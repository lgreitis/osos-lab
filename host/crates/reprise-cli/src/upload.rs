// SPDX-License-Identifier: GPL-3.0-only

use super::{
    bundle, open_session, parse_selector, print_json, print_report, read_bounded,
    write_check_event, CliResult, Progress,
};
use reprise_device::{DeviceSelector, UploadFile, UploadHelper, UploadOptions};
use std::{
    fs::File,
    io::{self, Write},
    path::Path,
};

#[derive(Debug, PartialEq, Eq)]
pub(super) struct Args {
    /// Input path and device destination, in upload order.
    files: Vec<(String, String)>,
    helper: Option<String>,
    bundle: Option<String>,
    key: Option<String>,
    overwrite: bool,
    selector: Option<DeviceSelector>,
}

pub(super) fn parse(args: &[String]) -> Result<Args, String> {
    let (mut helper, mut selector, mut bundle, mut key) = (None, None, None, None);
    let mut files: Vec<(String, String)> = Vec::new();
    let mut pending_input = None;
    let mut overwrite = false;
    let mut args = args.iter();
    while let Some(arg) = args.next() {
        if arg == "--overwrite" && !overwrite {
            overwrite = true;
            continue;
        }
        let value = args
            .next()
            .filter(|s| !s.starts_with('-'))
            .ok_or_else(|| format!("{arg} needs a value"))?;
        match arg.as_str() {
            "--input" if pending_input.is_none() => pending_input = Some(value.clone()),
            "--destination" if pending_input.is_some() => {
                files.push((pending_input.take().unwrap(), value.clone()));
            }
            "--input" | "--destination" => {
                return Err("Give --input FILE --destination /PATH in pairs".into())
            }
            "--helper" if helper.is_none() => helper = Some(value.clone()),
            "--bundle" if bundle.is_none() => bundle = Some(value.clone()),
            "--key" if key.is_none() => key = Some(value.clone()),
            "--device" if selector.is_none() => selector = Some(parse_selector(value)?),
            _ => return Err(format!("Unknown or duplicate upload argument: {arg}")),
        }
    }
    if !matches!(
        (&helper, &bundle, &key),
        (Some(_), None, None) | (None, Some(_), Some(_))
    ) {
        return Err(
            "Choose --helper BUILD_DIRECTORY or --bundle DIRECTORY --key PUBLIC_KEY_FILE".into(),
        );
    }
    if pending_input.is_some() {
        return Err("Missing --destination /PATH".into());
    }
    if files.is_empty() {
        return Err("Missing --input FILE --destination /PATH".into());
    }
    let mut destinations = std::collections::BTreeSet::new();
    for (_, destination) in &files {
        UploadOptions {
            destination: destination.clone(),
            overwrite,
        }
        .validate()
        .map_err(|e| e.to_string())?;
        if !destinations.insert(destination.to_ascii_lowercase()) {
            return Err(format!("Duplicate destination: {destination}"));
        }
    }
    Ok(Args {
        files,
        helper,
        bundle,
        key,
        overwrite,
        selector,
    })
}

pub(super) fn run(args: Args, json: bool) -> CliResult<u8> {
    let bundle = match (&args.bundle, &args.key) {
        (Some(directory), Some(key)) => Some(reprise_bundle::VerifiedBundle::load(
            Path::new(directory),
            &bundle::trusted_key(key)?,
            env!("CARGO_PKG_VERSION"),
        )?),
        _ => None,
    };
    let helper = match args.helper {
        Some(folder) => {
            let folder = Path::new(&folder);
            UploadHelper::from_bytes(
                &read_bounded(folder.join("upload.dfu"), 0x1f000)?,
                &read_bounded(folder.join("manifest.json"), 16384)?,
            )?
        }
        None => {
            let bundle = bundle.as_ref().ok_or("Missing helper source")?;
            UploadHelper::from_bytes(
                bundle.file("usb_helper", "image")?,
                bundle.file("usb_helper", "descriptor")?,
            )?
        }
    };
    let mut inputs = Vec::with_capacity(args.files.len());
    for (path, _) in &args.files {
        let input = File::open(path)?;
        let metadata = input.metadata()?;
        if !metadata.is_file() || metadata.len() > reprise_device::MAX_UPLOAD_SIZE {
            return Err(format!(
                "{path}: input must be a regular file, at most 2 GiB minus 1 byte"
            )
            .into());
        }
        inputs.push(input);
    }
    if args.files.len() > 1 && !helper.supports_batch_upload() {
        return Err("This helper takes one file per launch; upload the files one by one".into());
    }
    let mut session = open_session(args.selector)?;
    let checks = session.check(|e| {
        let _ = write_check_event(&e, json, &mut io::stderr().lock());
    });
    if !checks.compatible {
        return print_report(&checks, json);
    }
    if let Some(bundle) = bundle {
        let identity = checks.identity.as_ref().ok_or("Missing checked identity")?;
        bundle.require_device(
            identity.hardware_version,
            checks
                .bootrom_sha256
                .as_deref()
                .ok_or("Missing checked BootROM")?,
        )?;
    }
    let mut files: Vec<UploadFile<'_>> = inputs
        .iter_mut()
        .zip(&args.files)
        .map(|(input, (_, destination))| UploadFile {
            input,
            options: UploadOptions {
                destination: destination.clone(),
                overwrite: args.overwrite,
            },
        })
        .collect();
    let count = files.len();
    let mut progress = Progress::default();
    let mut current = None;
    let result = session.upload_files(
        &helper,
        &mut files,
        |p| {
            let mut err = io::stderr().lock();
            if count > 1 && p.stage != "hash" && current != Some(p.file) && !json {
                current = Some(p.file);
                progress = Progress::default();
                let _ = writeln!(err, "File {}/{count}: {}", p.file + 1, args.files[p.file].1);
            }
            let _ = progress.write(p.stage, p.completed, p.total, json, &mut err);
            Ok(())
        },
        |_| {},
    );
    match result {
        Ok(uploaded) => {
            if json {
                print_json(&serde_json::json!({
                    "schema_version": 2,
                    "files": uploaded.reports,
                    "cleanup": uploaded.cleanup,
                }))?;
            } else {
                let mut out = io::stdout().lock();
                for report in &uploaded.reports {
                    writeln!(
                        out,
                        "Uploaded and verified {} bytes to {} in {:.2}s\nSHA-256: {}",
                        report.bytes, report.destination, report.seconds, report.sha256
                    )?;
                }
                writeln!(out, "DFU: ready")?;
            }
            Ok(0)
        }
        Err(error) => {
            if json {
                print_json(&serde_json::json!({
                    "schema_version": 2,
                    "error": error.message,
                    "cleanup": error.cleanup,
                    "files": error.reports,
                }))?;
            } else {
                writeln!(
                    io::stderr().lock(),
                    "Error: {}\nVerified before the error: {}\nDFU cleanup: {:?}",
                    error.message,
                    error.reports.len(),
                    error.cleanup
                )?;
            }
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn upload_takes_input_destination_pairs_and_overwrite_is_opt_in() {
        let parse_line =
            |s: &str| parse(&s.split_whitespace().map(String::from).collect::<Vec<_>>());
        let single = parse_line("--input os --destination /osos.bin --helper helper").unwrap();
        assert!(!single.overwrite);
        assert_eq!(single.files, vec![("os".into(), "/osos.bin".into())]);
        let pair = parse_line(
            "--input a --destination /a --input b --destination /themes/b --helper helper --overwrite",
        )
        .unwrap();
        assert!(pair.overwrite);
        assert_eq!(
            pair.files,
            vec![("a".into(), "/a".into()), ("b".into(), "/themes/b".into())]
        );
        for line in [
            "--input os --destination /os --bundle b",
            "--input os --destination /os --bundle b --key k --helper h",
            "--input os --destination /os --helper h --key k",
            "--input os --helper h",
            "--destination /os --helper h",
            "--input a --input b --destination /b --helper h",
            "--input a --destination /a --destination /b --helper h",
            "--input a --destination /a --input b --destination /A --helper h",
            "--helper h",
        ] {
            assert!(parse_line(line).is_err(), "{line}");
        }
    }
}
