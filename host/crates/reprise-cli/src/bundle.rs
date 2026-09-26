// SPDX-License-Identifier: GPL-3.0-only

use super::{parse_options, print_json, read_bounded, CliResult};
use reprise_bundle::{TrustedKey, VerifiedBundle};
use std::{
    io::{self, Write},
    path::Path,
};

#[derive(Debug, PartialEq, Eq)]
pub(super) enum Args {
    ApplyRecipe {
        recipe: String,
        data: String,
        inputs: String,
        nor: Option<String>,
        out: String,
    },
    Assemble {
        directory: String,
        key: String,
        inputs: String,
        nor: String,
        out: String,
    },
    Inspect {
        directory: String,
        key: String,
    },
    Fetch {
        url: String,
        key: String,
        cache: String,
        digest: Option<String>,
    },
    Sign {
        directory: String,
        seed: String,
    },
}

pub(super) fn parse(args: &[String]) -> Result<Args, String> {
    let action = args
        .first()
        .map(String::as_str)
        .ok_or("Expected bundle inspect, fetch, sign, assemble or apply-recipe")?;
    let allowed: &[&str] = match action {
        "apply-recipe" => &["--recipe", "--data", "--inputs", "--nor", "--out"],
        "inspect" => &["--directory", "--key"],
        "fetch" => &["--url", "--key", "--cache", "--sha256"],
        "sign" => &["--directory", "--seed"],
        "assemble" => &["--directory", "--key", "--inputs", "--nor", "--out"],
        _ => return Err("Expected bundle inspect, fetch, sign, assemble or apply-recipe".into()),
    };
    let options = parse_options(&args[1..], allowed)?;
    let required = |name: &str| {
        options
            .get(name)
            .cloned()
            .ok_or_else(|| format!("Missing {name}"))
    };
    Ok(match action {
        "apply-recipe" => Args::ApplyRecipe {
            recipe: required("--recipe")?,
            data: required("--data")?,
            inputs: required("--inputs")?,
            nor: options.get("--nor").cloned(),
            out: required("--out")?,
        },
        "assemble" => Args::Assemble {
            directory: required("--directory")?,
            key: required("--key")?,
            inputs: required("--inputs")?,
            nor: required("--nor")?,
            out: required("--out")?,
        },
        "inspect" => Args::Inspect {
            directory: required("--directory")?,
            key: required("--key")?,
        },
        "fetch" => Args::Fetch {
            url: required("--url")?,
            key: required("--key")?,
            cache: required("--cache")?,
            digest: options.get("--sha256").cloned(),
        },
        "sign" => Args::Sign {
            directory: required("--directory")?,
            seed: required("--seed")?,
        },
        _ => unreachable!(),
    })
}

pub(super) fn trusted_key(path: &str) -> CliResult<TrustedKey> {
    Ok(TrustedKey::from_hex(std::str::from_utf8(&read_bounded(
        path, 128,
    )?)?)?)
}

pub(super) fn run(args: Args, json: bool) -> CliResult<u8> {
    let (directory, bundle) = match args {
        Args::ApplyRecipe {
            recipe,
            data,
            inputs,
            nor,
            out,
        } => {
            super::ensure_new_output(&out)?;
            let recipe = read_bounded(recipe, 2 * 1024 * 1024)?;
            let data = read_bounded(data, 0xc00000)?;
            let image = match nor {
                Some(nor) => reprise_bundle::assembly::assemble_local_companion(
                    &recipe,
                    &data,
                    Path::new(&inputs),
                    &read_bounded(nor, 0x100000)?,
                )?,
                None => {
                    reprise_bundle::assembly::assemble_local(&recipe, &data, Path::new(&inputs))?
                }
            };
            super::write_new_output(&out, &image)?;
            if json {
                print_json(&serde_json::json!({"output": out, "bytes": image.len()}))?;
            } else {
                writeln!(
                    io::stdout().lock(),
                    "Assembled {} bytes: {out}",
                    image.len()
                )?;
            }
            return Ok(0);
        }
        Args::Assemble {
            directory,
            key,
            inputs,
            nor,
            out,
        } => {
            let bundle = VerifiedBundle::load(
                Path::new(&directory),
                &trusted_key(&key)?,
                env!("CARGO_PKG_VERSION"),
            )?;
            let inputs = reprise_bundle::assembly::AppleInputs::load(Path::new(&inputs))?;
            let nor = read_bounded(nor, 0x100000)?;
            let artifacts = reprise_bundle::assembly::assemble(&bundle, &inputs, &nor)?;
            let report = artifacts.write(Path::new(&out))?;
            if json {
                print_json(&report)?;
            } else {
                writeln!(io::stdout().lock(), "Assembled firmware in {out}")?;
                for (name, file) in &report.files {
                    writeln!(
                        io::stdout().lock(),
                        "{name}: {} bytes, SHA-256 {}",
                        file.bytes,
                        file.sha256
                    )?;
                }
            }
            return Ok(0);
        }
        Args::Inspect { directory, key } => {
            let bundle = VerifiedBundle::load(
                Path::new(&directory),
                &trusted_key(&key)?,
                env!("CARGO_PKG_VERSION"),
            )?;
            (directory, bundle)
        }
        Args::Fetch {
            url,
            key,
            cache,
            digest,
        } => {
            let (directory, bundle) = reprise_bundle::fetch(
                &url,
                digest.as_deref(),
                &trusted_key(&key)?,
                Path::new(&cache),
                env!("CARGO_PKG_VERSION"),
            )?;
            (directory.to_string_lossy().into_owned(), bundle)
        }
        Args::Sign { directory, seed } => {
            let seed = read_bounded(seed, 128)?;
            let public = reprise_bundle::sign_directory(
                Path::new(&directory),
                std::str::from_utf8(&seed)?,
                env!("CARGO_PKG_VERSION"),
            )?;
            if json {
                print_json(&serde_json::json!({
                    "directory": directory,
                    "public_key": public,
                }))?;
            } else {
                writeln!(
                    io::stdout().lock(),
                    "Signed {directory}/manifest.json\nPublic key: {public}"
                )?;
            }
            return Ok(0);
        }
    };
    if json {
        print_json(&serde_json::json!({
            "directory": directory,
            "sha256": bundle.digest(),
            "manifest": bundle.manifest(),
        }))?;
    } else {
        writeln!(
            io::stdout().lock(),
            "Verified {} ({:?})\nSHA-256: {}\nDirectory: {}",
            bundle.manifest().version,
            bundle.manifest().purpose,
            bundle.digest(),
            directory
        )?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn commands_require_explicit_trust_and_unambiguous_options() {
        let parse_line =
            |s: &str| parse(&s.split_whitespace().map(str::to_owned).collect::<Vec<_>>());
        for line in [
            "",
            "fetch --url https://example.org/manifest.json --cache cache",
            "inspect --directory dir",
            "sign --directory dir --key key",
            "fetch --key k --key other",
            "inspect --directory --key k",
            "assemble --directory dir --key k --inputs apple --out out",
            "apply-recipe --recipe r --inputs apple --out out",
        ] {
            assert!(parse_line(line).is_err(), "{line}");
        }
        assert!(parse_line("apply-recipe --recipe r --data d --inputs apple --out out").is_ok());
        assert!(parse_line(
            "apply-recipe --recipe r --data d --inputs apple --nor backup --out out"
        )
        .is_ok());
        assert!(parse_line("inspect --directory dir --key key").is_ok());
        assert!(parse_line("sign --directory dir --seed secret").is_ok());
        assert!(parse_line(
            "assemble --directory dir --key k --inputs apple --nor backup --out out"
        )
        .is_ok());
        assert!(parse_line(
            "fetch --url https://example.org/manifest.json --cache cache --key key --sha256 hash"
        )
        .is_ok());
    }
}
