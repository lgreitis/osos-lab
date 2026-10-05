// SPDX-License-Identifier: GPL-3.0-only

use super::{
    ensure_new_output, open_session, parse_options, parse_selector, print_json, print_report,
    read_bounded, write_check_event, write_new_output, CliResult, Progress,
};
use reprise_bundle::preparation::{Ipsw, PreparedInputs};
use reprise_device::{AppleNorImage, DeviceSelector, SysCfg};
use std::{io, path::Path};

#[derive(Debug, PartialEq, Eq)]
pub(super) enum Args {
    ExtractEfi {
        input: String,
        out: String,
    },
    Inspect {
        ipsw: String,
    },
    Extract {
        ipsw: String,
        output: String,
    },
    Prepare {
        ipsw: String,
        nor: String,
        osos: String,
        loader: Option<String>,
        out: String,
    },
    DecryptNor {
        input: String,
        output: String,
        selector: Option<DeviceSelector>,
    },
}

pub(super) fn parse(args: &[String]) -> Result<Args, String> {
    let action = args
        .first()
        .map(String::as_str)
        .ok_or("Expected firmware inspect, extract, extract-efi, prepare or decrypt-nor")?;
    let allowed: &[&str] = match action {
        "extract-efi" => &["--input", "--out"],
        "inspect" => &["--ipsw"],
        "extract" => &["--ipsw", "--output"],
        "prepare" => &["--ipsw", "--nor", "--osos", "--apple-loader", "--out"],
        "decrypt-nor" => &["--input", "--output", "--device"],
        _ => {
            return Err(
                "Expected firmware inspect, extract, extract-efi, prepare or decrypt-nor".into(),
            )
        }
    };
    let values = parse_options(&args[1..], allowed)?;
    let required = |name: &str| {
        values
            .get(name)
            .cloned()
            .ok_or_else(|| format!("Missing {name}"))
    };
    Ok(match action {
        "extract-efi" => Args::ExtractEfi {
            input: required("--input")?,
            out: required("--out")?,
        },
        "inspect" => Args::Inspect {
            ipsw: required("--ipsw")?,
        },
        "extract" => Args::Extract {
            ipsw: required("--ipsw")?,
            output: required("--output")?,
        },
        "prepare" => Args::Prepare {
            ipsw: required("--ipsw")?,
            nor: required("--nor")?,
            osos: required("--osos")?,
            loader: values.get("--apple-loader").cloned(),
            out: required("--out")?,
        },
        "decrypt-nor" => Args::DecryptNor {
            input: required("--input")?,
            output: required("--output")?,
            selector: values
                .get("--device")
                .map(|v| parse_selector(v))
                .transpose()?,
        },
        _ => unreachable!(),
    })
}

pub(super) fn run(args: Args, json: bool) -> CliResult<u8> {
    let report = match args {
        Args::ExtractEfi { input, out } => {
            let loader = read_bounded(input, 0x100000)?;
            let modules = reprise_bundle::preparation::extract_efi_modules(&loader)?;
            let mut files = std::collections::BTreeMap::new();
            let mut inventory = Vec::new();
            for module in modules {
                let filename = format!("{}-{}.{}", module.name, module.guid, module.format);
                let mut item = serde_json::to_value(&module)?;
                item["file"] = filename.clone().into();
                item["bytes"] = module.bytes.len().into();
                if files.insert(filename, module.bytes).is_some() {
                    return Err("Duplicate EFI executable filename".into());
                }
                inventory.push(item);
            }
            files.insert(
                "modules.json".into(),
                serde_json::to_vec_pretty(&inventory)?,
            );
            if Path::new(&out).exists() {
                return Err("EFI output directory already exists".into());
            }
            std::fs::create_dir_all(&out)?;
            for (name, bytes) in files {
                write_new_output(&Path::new(&out).join(name).to_string_lossy(), &bytes)?;
            }
            serde_json::json!({"directory": out, "modules": inventory.len()})
        }
        Args::Inspect { ipsw } => {
            let ipsw = Ipsw::load(Path::new(&ipsw))?;
            serde_json::json!({
                "version": ipsw.metadata.version(),
                "metadata": ipsw.metadata,
                "sha256": ipsw.sha256,
                "osos_bytes": ipsw.encrypted_osos().len(),
            })
        }
        Args::Extract { ipsw, output } => {
            ensure_new_output(&output)?;
            let ipsw = Ipsw::load(Path::new(&ipsw))?;
            write_new_output(&output, ipsw.encrypted_osos())?;
            serde_json::json!({
                "output": output,
                "version": ipsw.metadata.version(),
                "bytes": ipsw.encrypted_osos().len(),
            })
        }
        Args::Prepare {
            ipsw,
            nor,
            osos,
            loader,
            out,
        } => {
            let ipsw = Ipsw::load(Path::new(&ipsw))?;
            let nor = read_bounded(nor, 0x100000)?;
            let osos = read_bounded(osos, ipsw.encrypted_osos().len())?;
            let osos = if osos.len() == ipsw.ciphertext().len() {
                ipsw.wrap_plaintext(&osos)?
            } else {
                osos
            };
            let loader = loader
                .map(|p| read_bounded(p, reprise_device::APPLE_LOADER_BYTES))
                .transpose()?;
            let prepared = PreparedInputs::from_plaintext(&ipsw, &nor, &osos, loader.as_deref())?;
            prepared.write(Path::new(&out))?;
            serde_json::json!({
                "directory": out,
                "version": ipsw.metadata.version(),
                "files": 8,
            })
        }
        Args::DecryptNor {
            input,
            output,
            selector,
        } => {
            ensure_new_output(&output)?;
            let nor = read_bounded(input, 0x100000)?;
            let image = AppleNorImage::locate(&nor)?;
            let offset = image.offset;
            let encrypted = image.encrypted();
            let plain = if encrypted {
                let mut session = open_session(selector)?;
                let checks = session.check(|event| {
                    let _ = write_check_event(&event, json, &mut io::stderr().lock());
                });
                if !checks.compatible {
                    return print_report(&checks, json);
                }
                let saved = SysCfg::parse(&nor)?.identity()?;
                let live = checks.identity.as_ref().ok_or("Missing device identity")?;
                if saved.serial != live.serial || saved.hardware_id != live.hardware_id {
                    return Err("NOR backup belongs to a different device".into());
                }
                let mut progress = Progress::default();
                session.decrypt_nor(&nor, |p| {
                    let _ = progress.write(
                        p.stage,
                        p.completed as u64,
                        p.total as u64,
                        json,
                        &mut io::stderr().lock(),
                    );
                    Ok(())
                })?
            } else {
                image.plaintext()?.to_vec()
            };
            write_new_output(&output, &plain)?;
            serde_json::json!({
                "output": output,
                "nor_offset": offset,
                "decrypted": encrypted,
                "bytes": plain.len(),
                "sha256": reprise_device::APPLE_LOADER_SHA256,
            })
        }
    };
    print_json(&report)?;
    Ok(0)
}
