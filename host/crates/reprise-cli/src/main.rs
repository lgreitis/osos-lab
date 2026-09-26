// SPDX-License-Identifier: GPL-3.0-only

use reprise_device::{
    check_saved, discover, CheckReport, Cleanup, DeviceSelector, Event, Session, SysCfg,
};
mod bundle;
mod decrypt;
mod firmware;
mod progress;
mod upload;
use progress::Progress;
use serde::Serialize;
use std::{
    collections::BTreeMap,
    env,
    fs::{File, OpenOptions},
    io::{self, Read, Write},
    path::Path,
    process::ExitCode,
};

const HELP: &str = r#"Reprise device tools

Usage:
  reprise upload --input FILE --destination /PATH
    (--helper BUILD_DIRECTORY | --bundle DIRECTORY --key PUBLIC_KEY_FILE)
    [--overwrite] [--device BUS:ADDRESS] [--json]
  reprise bundle inspect --directory DIR --key PUBLIC_KEY_FILE [--json]
  reprise bundle fetch --url HTTPS_MANIFEST_URL --key PUBLIC_KEY_FILE
    --cache DIR [--sha256 MANIFEST_SHA256] [--json]
  reprise bundle sign --directory DIR --seed SECRET_SEED_FILE [--json]
  reprise bundle assemble --directory DIR --key PUBLIC_KEY_FILE
    --inputs DECRYPTED_APPLE_DIR --nor BACKUP_FILE --out NEW_DIR [--json]
  reprise bundle apply-recipe --recipe FILE --data FILE
    --inputs DECRYPTED_APPLE_DIR --out NEW_FILE [--nor BACKUP_FILE] [--json]
  reprise firmware inspect --ipsw FILE [--json]
  reprise firmware extract --ipsw FILE --output NEW_IMG1 [--json]
  reprise firmware prepare --ipsw FILE --nor BACKUP --osos PLAINTEXT
    [--apple-loader PLAINTEXT_BODY] --out NEW_DIR [--json]
  reprise firmware decrypt-nor --input BACKUP --output NEW_FILE
    [--device BUS:ADDRESS] [--json]
  reprise devices [--json]
  reprise check [--device BUS:ADDRESS] [--json]
  reprise nor-dump --output NEW_FILE [--device BUS:ADDRESS] [--json]
  reprise check-files --bootrom FILE --syscfg FILE [--json]
  reprise syscfg FILE [--json]

  reprise decrypt --input FILE --output NEW_FILE
    [--skip-bytes BYTES] [--offset BYTES] [--size BYTES] [--chunk BYTES] [--iv HEX]
    [--reference FILE] [--device BUS:ADDRESS] [--json]

upload streams through a 64 KiB buffer, verifies disk readback, then returns to DFU.
Use a signed --bundle with its trusted --key, or a development --helper directory. Existing destination
files require --overwrite; replacement happens after temporary-file verification.
Files may have any byte length up to 2 GiB minus 1 byte; directories must exist.

decrypt reads the Classic IMG1 header and decrypts its entire encrypted body.
Output contains the plaintext body, including final AES block padding.
For raw ciphertext, --skip-bytes N skips N input bytes and decrypts the rest;
use --skip-bytes 0 for a file containing only ciphertext.
--offset starts inside the body; --size limits the bytes decrypted.
Offset and size must be multiples of 16. Numbers accept decimal or 0x.
CBC uses the preceding ciphertext block for interior offsets, or --iv
(32 hex digits, default zero) at body offset zero.
--chunk defaults to 512 (16..512, aligned to 16).
--reference compares against a plaintext body file at the same --offset.
Output paths must be new.

check uses 512-byte BootROM replies after a small ROM/transport preflight,
verifies the full 64 KiB fingerprint, then performs bounded
NOR SysCfg reads. Connect in BootROM DFU (blank screen);
recoverable DFU states are cleared before checks. Reset only if recovery fails.
Supported target: Classic Rev B / 2.0.4.

--device uses decimal BUS:ADDRESS from `reprise devices`, not USB VID:PID.
Omit it when only one DFU device is connected.

check-files accepts saved SysCfg or a NOR dump beginning with SysCfg.
nor-dump checks the device, then reads/cross-checks the full 1 MiB NOR using
512-byte data replies, shifted reads, and slow edge reads.
--json suppresses text progress and emits one final result to stdout.
Otherwise live progress goes to stderr.
Exit codes: 0 success, 1 command/access/input error, 2 checks failed.
"#;

#[derive(Debug, PartialEq, Eq)]
enum Command {
    Help,
    Bundle(bundle::Args),
    Firmware(firmware::Args),
    Devices,
    Check {
        selector: Option<DeviceSelector>,
    },
    CheckFiles {
        bootrom: String,
        syscfg: String,
    },
    Syscfg {
        path: String,
    },
    Decrypt(decrypt::Args),
    Upload(upload::Args),
    NorDump {
        output: String,
        selector: Option<DeviceSelector>,
    },
}

fn parse_options<'a>(
    args: &'a [String],
    allowed: &[&str],
) -> Result<BTreeMap<&'a str, String>, String> {
    let mut options = BTreeMap::new();
    let mut rest = args.iter();
    while let Some(option) = rest.next() {
        if !allowed.contains(&option.as_str()) || options.contains_key(option.as_str()) {
            return Err(format!("Unknown or duplicate argument: {option}"));
        }
        let value = rest
            .next()
            .filter(|value| !value.starts_with('-'))
            .ok_or_else(|| format!("{option} needs a value"))?;
        options.insert(option.as_str(), value.clone());
    }
    Ok(options)
}

fn parse(args: &[String]) -> Result<(Command, bool), String> {
    let mut args = args.to_vec();
    let json_count = args.iter().filter(|s| s.as_str() == "--json").count();
    if json_count > 1 {
        return Err("Duplicate --json".into());
    }
    args.retain(|s| s != "--json");
    let json = json_count == 1;
    if args.len() == 2
        && matches!(
            args[0].as_str(),
            "devices"
                | "check"
                | "nor-dump"
                | "decrypt"
                | "check-files"
                | "syscfg"
                | "upload"
                | "bundle"
                | "firmware"
        )
        && matches!(args[1].as_str(), "--help" | "-h")
    {
        return Ok((Command::Help, json));
    }
    let command = match args.first().map(String::as_str) {
        None | Some("help" | "--help" | "-h") if args.len() <= 1 => Command::Help,
        Some("devices") if args.len() == 1 => Command::Devices,
        Some("syscfg") if args.len() == 2 && !args[1].starts_with('-') => Command::Syscfg {
            path: args[1].clone(),
        },
        Some("check" | "nor-dump") => {
            let nor_dump = args[0] == "nor-dump";
            let mut selector = None;
            let mut output = None;
            let mut rest = args[1..].iter();
            while let Some(arg) = rest.next() {
                match arg.as_str() {
                    "--output" if nor_dump && output.is_none() => {
                        output = Some(
                            rest.next()
                                .filter(|p| !p.starts_with('-'))
                                .ok_or("--output needs a new file path")?
                                .clone(),
                        );
                    }
                    "--device" if selector.is_none() => {
                        let value = rest.next().ok_or("--device needs BUS:ADDRESS")?;
                        selector = Some(parse_selector(value)?);
                    }
                    _ => return Err(format!("Unknown or duplicate {} argument: {arg}", args[0])),
                }
            }
            if nor_dump {
                Command::NorDump {
                    output: output.ok_or("Missing --output NEW_FILE")?,
                    selector,
                }
            } else {
                Command::Check { selector }
            }
        }
        Some("firmware") => Command::Firmware(firmware::parse(&args[1..])?),
        Some("bundle") => Command::Bundle(bundle::parse(&args[1..])?),
        Some("upload") => Command::Upload(upload::parse(&args[1..])?),
        Some("decrypt") => Command::Decrypt(decrypt::parse(&args[1..])?),
        Some("check-files") => {
            let mut bootrom = None;
            let mut syscfg = None;
            let mut rest = args[1..].iter();
            while let Some(arg) = rest.next() {
                let slot = match arg.as_str() {
                    "--bootrom" if bootrom.is_none() => &mut bootrom,
                    "--syscfg" if syscfg.is_none() => &mut syscfg,
                    _ => return Err(format!("Unknown or duplicate check-files argument: {arg}")),
                };
                let path = rest.next().ok_or_else(|| format!("{arg} needs a path"))?;
                if path.starts_with('-') {
                    return Err(format!(
                        "{arg} needs a path (prefix option-like filenames with ./)"
                    ));
                }
                *slot = Some(path.clone());
            }
            Command::CheckFiles {
                bootrom: bootrom.ok_or("Missing --bootrom FILE")?,
                syscfg: syscfg.ok_or("Missing --syscfg FILE")?,
            }
        }
        _ => return Err("Invalid command; use reprise --help".into()),
    };
    Ok((command, json))
}

type CliResult<T> = Result<T, Box<dyn std::error::Error>>;

fn open_session(selector: Option<DeviceSelector>) -> CliResult<Session> {
    Session::open(selector).map_err(|error| match error {
        reprise_device::Error::DeviceCount(_) => {
            format!("{error}; use `reprise devices` and --device BUS:ADDRESS to select an iPod")
                .into()
        }
        error => error.into(),
    })
}

fn parse_selector(value: &str) -> Result<DeviceSelector, String> {
    let message = "--device needs decimal BUS:ADDRESS from `reprise devices`, not USB VID:PID; omit it for a single device";
    let (bus, address) = value.split_once(':').ok_or(message)?;
    if [bus, address]
        .iter()
        .any(|s| s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()))
    {
        return Err(message.into());
    }
    Ok(DeviceSelector {
        bus: bus.parse().map_err(|_| message)?,
        address: address.parse().map_err(|_| message)?,
    })
}

fn ensure_new_output(path: &str) -> CliResult<()> {
    match std::fs::symlink_metadata(path) {
        Ok(_) => Err("Output already exists; choose a new path".into()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(e) => Err(e.into()),
    }
}

fn write_new_output(path: &str, bytes: &[u8]) -> CliResult<()> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(path)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    Ok(())
}

fn read_bounded(path: impl AsRef<Path>, max: usize) -> CliResult<Vec<u8>> {
    let mut bytes = Vec::new();
    File::open(path)?
        .take(max as u64 + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() > max {
        return Err(format!("Input exceeds {max} bytes").into());
    }
    Ok(bytes)
}

fn print_json(value: &impl Serialize) -> CliResult<()> {
    let mut stdout = io::stdout().lock();
    serde_json::to_writer_pretty(&mut stdout, value)?;
    writeln!(stdout)?;
    Ok(())
}

fn print_report(report: &CheckReport, json: bool) -> CliResult<u8> {
    write_report(report, json, &mut io::stdout().lock())
}

fn write_report(report: &CheckReport, json: bool, out: &mut impl Write) -> CliResult<u8> {
    if json {
        serde_json::to_writer_pretty(&mut *out, report)?;
        writeln!(out)?;
    } else {
        for check in &report.checks {
            writeln!(out, "{:?}: {:?} — {}", check.id, check.status, check.detail)?;
        }
        if let Some(identity) = &report.identity {
            writeln!(out, "Serial: {}", identity.serial)?;
        }
        writeln!(out, "DFU cleanup: {:?}", report.cleanup)?;
        if let Cleanup::Failed(_) = report.cleanup {
            writeln!(out, "Re-enter fresh DFU before another operation.")?;
        }
        writeln!(out, "Target compatible: {}", report.compatible)?;
    }
    Ok(if report.compatible { 0 } else { 2 })
}

fn write_check_event(event: &Event, json: bool, out: &mut impl Write) -> io::Result<()> {
    if json {
        return Ok(());
    }
    match event {
        Event::Check(check) if check.status == reprise_device::CheckStatus::Running => {
            let label = match check.id {
                reprise_device::CheckId::Rom => "Reading BootROM",
                reprise_device::CheckId::Model => "Reading device information",
                _ => "Checking DFU",
            };
            writeln!(out, "{label}…")
        }
        _ => Ok(()),
    }
}

fn run(command: Command, json: bool) -> CliResult<u8> {
    match command {
        Command::Help => {
            io::stdout().lock().write_all(HELP.as_bytes())?;
        }
        Command::Devices => {
            let devices = discover()?;
            if json {
                print_json(&devices)?;
            } else {
                let mut out = io::stdout().lock();
                if devices.is_empty() {
                    writeln!(out, "No relevant Apple USB devices found")?;
                }
                for device in devices {
                    writeln!(
                        out,
                        "{}:{}  {:04x}:{:04x}  {}",
                        device.selector.bus,
                        device.selector.address,
                        device.vendor_id,
                        device.product_id,
                        device.mode
                    )?;
                }
            }
        }
        Command::Check { selector } => {
            let mut session = open_session(selector)?;
            let report = session.check(|event| {
                // Ignore logging failures so DFU cleanup still runs.
                let _ = write_check_event(&event, json, &mut io::stderr().lock());
            });
            return print_report(&report, json);
        }
        Command::CheckFiles { bootrom, syscfg } => {
            let rom = read_bounded(bootrom, reprise_device::BOOTROM_SIZE)?;
            let cfg = read_bounded(syscfg, 0x100000)?;
            return print_report(&check_saved(&rom, &cfg), json);
        }
        Command::Syscfg { path } => {
            let bytes = read_bounded(path, 0x100000)?;
            let config = SysCfg::parse(&bytes)?;
            let identity = config.identity()?;
            if json {
                print_json(&identity)?;
            } else {
                let mut out = io::stdout().lock();
                writeln!(
                    out,
                    "Model: {}\nSerial: {}\nHardware: {:#010x}\nRecorded firmware: {}",
                    identity.model,
                    identity.serial,
                    identity.hardware_version,
                    identity.recorded_firmware
                )?;
            }
        }
        Command::Firmware(args) => return firmware::run(args, json),
        Command::Bundle(args) => return bundle::run(args, json),
        Command::Upload(args) => return upload::run(args, json),
        Command::Decrypt(args) => return decrypt::run(args, json),
        Command::NorDump { output, selector } => {
            ensure_new_output(&output)?;
            let mut session = open_session(selector)?;
            let checks = session.check(|event| {
                let _ = write_check_event(&event, json, &mut io::stderr().lock());
            });
            if !checks.compatible {
                return print_report(&checks, json);
            }
            let mut progress = Progress::default();
            let dump = session.dump_nor(|p| {
                let _ = progress.write(
                    p.stage,
                    p.completed as u64,
                    p.total as u64,
                    json,
                    &mut io::stderr().lock(),
                );
                Ok(())
            })?;
            write_new_output(&output, &dump.bytes)?;
            if json {
                print_json(&serde_json::json!({
                    "checks": checks,
                    "nor_dump": dump.report,
                    "output": output,
                }))?;
            } else {
                let mut out = io::stdout().lock();
                writeln!(
                    out,
                    "Read and cross-checked {} NOR bytes in {:.3}s",
                    dump.report.bytes, dump.report.seconds
                )?;
                writeln!(
                    out,
                    "SHA-256: {}\nDFU cleanup: {:?}\nOutput: {}",
                    dump.report.sha256, dump.report.cleanup, output
                )?;
            }
        }
    }
    Ok(0)
}

fn main() -> ExitCode {
    let args: Vec<_> = env::args().skip(1).collect();
    let json = args.iter().any(|s| s == "--json");
    let result = parse(&args)
        .map_err(Into::into)
        .and_then(|(command, json)| run(command, json));
    match result {
        Ok(code) => ExitCode::from(code),
        Err(error) => {
            if json {
                let _ = print_json(&serde_json::json!({
                    "schema_version": 1,
                    "error": error.to_string(),
                }));
            } else {
                let _ = writeln!(io::stderr().lock(), "Error: {error}");
            }
            ExitCode::from(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(s: &str) -> Vec<String> {
        s.split_whitespace().map(String::from).collect()
    }

    #[test]
    fn live_checks_accept_existing_dfu_and_require_strict_selection() {
        assert_eq!(
            parse(&args("check")).unwrap(),
            (Command::Check { selector: None }, false)
        );
        assert!(parse(&args("check --device 1:2:3")).is_err());
        assert!(parse(&args("check --device 999:1")).is_err());
        assert!(parse(&args("check --fresh-dfu")).is_err());
        assert!(parse(&args("check --reset")).is_err());
        assert_eq!(
            parse(&args("check --json --device 1:2")).unwrap(),
            (
                Command::Check {
                    selector: Some(DeviceSelector { bus: 1, address: 2 })
                },
                true
            )
        );
    }

    #[test]
    fn offline_commands_require_complete_unambiguous_inputs() {
        assert!(parse(&args("check-files --bootrom rom")).is_err());
        assert!(parse(&args("check-files --bootrom --syscfg cfg")).is_err());
        assert!(parse(&args("check-files --bootrom a --bootrom b --syscfg c")).is_err());
        assert!(parse(&args("devices --json --json")).is_err());
        assert!(parse(&args("devices garbage")).is_err());
        assert_eq!(
            parse(&args("check-files --syscfg cfg --bootrom rom --json")).unwrap(),
            (
                Command::CheckFiles {
                    bootrom: "rom".into(),
                    syscfg: "cfg".into()
                },
                true
            )
        );
    }

    #[test]
    fn nor_dump_requires_a_new_output_argument() {
        assert!(parse(&args("nor-dump")).is_err());
        assert!(parse(&args("nor-dump --output --json")).is_err());
        assert!(parse(&args("nor-dump --output a --output b")).is_err());
        assert_eq!(
            parse(&args("nor-dump --output nor.bin --json")).unwrap(),
            (
                Command::NorDump {
                    output: "nor.bin".into(),
                    selector: None
                },
                true
            )
        );
    }

    #[test]
    fn selectors_help_and_removed_flag_are_consistent_across_live_commands() {
        for command in [
            "check",
            "nor-dump --output nor",
            "decrypt --input cipher --output plain --size 16",
        ] {
            for selector in ["05ac:1223", "1:256", "1:2:3", ":2", "+1:2"] {
                let error = parse(&args(&format!("{command} --device {selector}"))).unwrap_err();
                assert!(error.contains("decimal BUS:ADDRESS"), "{error}");
            }
            assert!(parse(&args(&format!("{command} --device 1:2 --device 1:3"))).is_err());
            assert!(parse(&args(&format!("{command} --fresh-dfu"))).is_err());
        }
        for command in [
            "check",
            "nor-dump",
            "decrypt",
            "devices",
            "check-files",
            "syscfg",
        ] {
            assert_eq!(
                parse(&args(&format!("{command} --help"))).unwrap().0,
                Command::Help
            );
        }
    }

    #[test]
    fn exclusive_output_preserves_existing_files() {
        let path = env::temp_dir().join(format!("reprise-output-test-{}", std::process::id()));
        let path = path.to_str().unwrap();
        write_new_output(path, b"original").unwrap();
        assert!(ensure_new_output(path).is_err());
        assert!(write_new_output(path, b"replacement").is_err());
        assert_eq!(std::fs::read(path).unwrap(), b"original");
        std::fs::remove_file(path).unwrap();
    }

    #[test]
    fn json_check_output_is_one_report_without_text_progress() {
        let report = check_saved(&[0; 64], &[]);
        let mut stderr = Vec::new();
        let events = [
            Event::Check(report.checks[0].clone()),
            Event::Progress {
                stage: reprise_device::CheckId::Rom,
                completed: 1024,
                total: 65536,
            },
        ];
        for event in &events {
            write_check_event(event, true, &mut stderr).unwrap();
        }
        for stage in ["echo", "decrypt", "nor-read", "nor-cross-check"] {
            Progress::default()
                .write(stage, 512, 1024, true, &mut stderr)
                .unwrap();
        }
        assert!(stderr.is_empty());
        let mut stdout = Vec::new();
        assert_eq!(write_report(&report, true, &mut stdout).unwrap(), 2);
        let parsed: serde_json::Value = serde_json::from_slice(&stdout).unwrap();
        assert_eq!(parsed["schema_version"], 1);
        assert_eq!(parsed["compatible"], false);
        for event in &events {
            write_check_event(event, false, &mut stderr).unwrap();
        }
        assert!(stderr.is_empty());
        let mut check = report.checks[3].clone();
        check.status = reprise_device::CheckStatus::Running;
        write_check_event(&Event::Check(check.clone()), false, &mut stderr).unwrap();
        check.status = reprise_device::CheckStatus::Passed;
        write_check_event(&Event::Check(check), false, &mut stderr).unwrap();
        assert_eq!(String::from_utf8(stderr).unwrap(), "Reading BootROM…\n");
    }
}
