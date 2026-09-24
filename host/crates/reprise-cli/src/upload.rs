// SPDX-License-Identifier: GPL-3.0-only

use super::{
    bundle, open_session, parse_selector, print_json, print_report, read_bounded,
    write_check_event, CliResult, Progress,
};
use reprise_device::{DeviceSelector, UploadHelper, UploadOptions};
use std::{
    fs::File,
    io::{self, Write},
    path::Path,
};

#[derive(Debug, PartialEq, Eq)]
pub(super) struct Args {
    input: String,
    destination: String,
    helper: Option<String>,
    bundle: Option<String>,
    key: Option<String>,
    overwrite: bool,
    selector: Option<DeviceSelector>,
}

pub(super) fn parse(args: &[String]) -> Result<Args, String> {
    let (mut input, mut destination, mut helper, mut selector) = (None, None, None, None);
    let (mut bundle, mut key) = (None, None);
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
            "--input" if input.is_none() => input = Some(value.clone()),
            "--destination" if destination.is_none() => destination = Some(value.clone()),
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
    let result = Args {
        input: input.ok_or("Missing --input FILE")?,
        destination: destination.ok_or("Missing --destination /PATH")?,
        helper,
        bundle,
        key,
        overwrite,
        selector,
    };
    UploadOptions {
        destination: result.destination.clone(),
        overwrite,
    }
    .validate()
    .map_err(|e| e.to_string())?;
    Ok(result)
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
    let mut input = File::open(&args.input)?;
    let metadata = input.metadata()?;
    if !metadata.is_file() || metadata.len() > reprise_device::MAX_UPLOAD_SIZE {
        return Err("Input must be a regular file, at most 2 GiB minus 1 byte".into());
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
            &identity.model,
            identity.hardware_version,
            &identity.recorded_firmware,
            reprise_device::SUPPORTED_BOOTROM_SHA256,
        )?;
    }
    let mut progress = Progress::default();
    let result = session.upload(
        &helper,
        &mut input,
        UploadOptions {
            destination: args.destination,
            overwrite: args.overwrite,
        },
        |p| {
            let _ = progress.write(
                p.stage,
                p.completed,
                p.total,
                json,
                &mut io::stderr().lock(),
            );
        },
    );
    match result {
        Ok(uploaded) => {
            if json {
                print_json(&uploaded.report)?;
            } else {
                writeln!(
                    io::stdout().lock(),
                    "Uploaded and verified {} bytes to {} in {:.2}s\nSHA-256: {}\nDFU: ready",
                    uploaded.report.bytes,
                    uploaded.report.destination,
                    uploaded.report.seconds,
                    uploaded.report.sha256
                )?;
            }
            Ok(0)
        }
        Err(error) => {
            if json {
                print_json(&serde_json::json!({
                    "schema_version": 1,
                    "error": error.message,
                    "cleanup": error.cleanup,
                }))?;
            } else {
                writeln!(
                    io::stderr().lock(),
                    "Error: {}\nDFU cleanup: {:?}",
                    error.message,
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
    fn upload_requires_explicit_paths_and_overwrite_is_opt_in() {
        let parse_line =
            |s: &str| parse(&s.split_whitespace().map(String::from).collect::<Vec<_>>());
        assert!(parse_line("--input os --destination /osos.bin").is_err());
        assert!(
            parse_line("--input os --destination /osos.bin --bundle release --key public").is_ok()
        );
        assert!(
            !parse_line("--input os --destination /osos.bin --helper helper")
                .unwrap()
                .overwrite
        );
        assert!(
            parse_line("--input os --destination /osos.bin --helper helper --overwrite")
                .unwrap()
                .overwrite
        );
        for line in [
            "--input os",
            "--input os --destination /../os --helper h",
            "--input os --destination /os --helper h --overwrite --overwrite",
            "--input os --input other --destination /os --helper h",
            "--input os --destination /os --helper h --device 05ac:1223",
            "--input os --destination /os --bundle b",
            "--input os --destination /os --bundle b --key k --helper h",
            "--input os --destination /os --helper h --key k",
        ] {
            assert!(parse_line(line).is_err(), "{line}");
        }
    }
}
