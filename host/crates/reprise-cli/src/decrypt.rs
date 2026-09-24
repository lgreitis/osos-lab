// SPDX-License-Identifier: GPL-3.0-only

use super::{
    ensure_new_output, parse_selector, print_json, print_report, write_check_event,
    write_new_output, CliResult, Progress,
};
use reprise_device::{classic_img1_body_range, DecryptOptions, DeviceSelector};
use std::{
    collections::BTreeMap,
    io::{self, Write},
};

#[derive(Debug, PartialEq, Eq)]
pub(super) struct Args {
    input: String,
    output: String,
    reference: Option<String>,
    skip_bytes: Option<usize>,
    offset: usize,
    size: Option<usize>,
    chunk: usize,
    iv: [u8; 16],
    selector: Option<DeviceSelector>,
}

fn number(value: &str) -> Result<usize, String> {
    let result = if let Some(hex) = value.strip_prefix("0x") {
        usize::from_str_radix(hex, 16)
    } else {
        value.parse()
    };
    result.map_err(|_| format!("Invalid byte count: {value}"))
}

pub(super) fn parse(args: &[String]) -> Result<Args, String> {
    let mut values = BTreeMap::new();
    let mut rest = args.iter();
    while let Some(arg) = rest.next() {
        if !matches!(
            arg.as_str(),
            "--input"
                | "--output"
                | "--reference"
                | "--skip-bytes"
                | "--body-offset"
                | "--offset"
                | "--size"
                | "--chunk"
                | "--iv"
                | "--device"
        ) || values.contains_key(arg.as_str())
        {
            return Err(format!("Unknown or duplicate decrypt argument: {arg}"));
        }
        let value = rest
            .next()
            .filter(|v| !v.starts_with('-'))
            .ok_or_else(|| format!("{arg} needs a value"))?;
        values.insert(arg.as_str(), value.as_str());
    }
    let required = |name| {
        values
            .get(name)
            .copied()
            .ok_or_else(|| format!("Missing {name}"))
    };
    let input = required("--input")?.into();
    let output = required("--output")?.into();
    let size = values
        .get("--size")
        .map(|value| number(value))
        .transpose()?;
    if values.contains_key("--skip-bytes") && values.contains_key("--body-offset") {
        return Err("Use --skip-bytes once; --body-offset is its legacy alias".into());
    }
    let skip_bytes = values
        .get("--skip-bytes")
        .or_else(|| values.get("--body-offset"))
        .map(|value| number(value))
        .transpose()?;
    let offset = number(values.get("--offset").unwrap_or(&"0"))?;
    let chunk = number(values.get("--chunk").unwrap_or(&"512"))?;
    if size.is_some_and(|n| n == 0 || !n.is_multiple_of(16)) || !offset.is_multiple_of(16) {
        return Err(
            "Size must be nonzero; size and body-relative offset must be 16-byte aligned".into(),
        );
    }
    if !(16..=512).contains(&chunk) || !chunk.is_multiple_of(16) {
        return Err("Chunk must be 16..512, aligned to 16".into());
    }
    skip_bytes
        .unwrap_or(0)
        .checked_add(offset)
        .and_then(|n| n.checked_add(size.unwrap_or(0)))
        .ok_or("Decryption range overflow")?;
    let mut iv = [0; 16];
    if let Some(hex) = values.get("--iv") {
        if hex.len() != 32 || !hex.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err("IV must contain exactly 32 hexadecimal digits".into());
        }
        for (i, byte) in iv.iter_mut().enumerate() {
            *byte = u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).map_err(|_| "Invalid IV")?;
        }
    }
    let selector = values
        .get("--device")
        .map(|value| parse_selector(value))
        .transpose()?;
    Ok(Args {
        input,
        output,
        reference: values.get("--reference").map(|s| s.to_string()),
        skip_bytes,
        offset,
        size,
        chunk,
        iv,
        selector,
    })
}

fn sample<'a>(bytes: &'a [u8], args: &Args) -> CliResult<(&'a [u8], [u8; 16])> {
    let body = match args.skip_bytes {
        Some(skip) => bytes
            .get(skip..)
            .ok_or("--skip-bytes exceeds input length")?,
        None => {
            &bytes[classic_img1_body_range(bytes)
                .map_err(|error| format!("{error}; for raw ciphertext, use --skip-bytes N"))?]
        }
    };
    let remaining = body
        .len()
        .checked_sub(args.offset)
        .ok_or("--offset exceeds body length")?;
    let size = args.size.unwrap_or(remaining);
    let end = args
        .offset
        .checked_add(size)
        .ok_or("Decryption range overflow")?;
    let data = body
        .get(args.offset..end)
        .ok_or("Decryption range exceeds body length")?;
    let mut iv = args.iv;
    if args.offset != 0 {
        iv.copy_from_slice(&body[args.offset - 16..args.offset]);
    }
    DecryptOptions {
        iv,
        chunk_size: args.chunk,
    }
    .validate(data, None)?;
    Ok((data, iv))
}

fn reference_sample<'a>(bytes: &'a [u8], args: &Args, size: usize) -> CliResult<&'a [u8]> {
    let end = args
        .offset
        .checked_add(size)
        .ok_or("Reference range overflow")?;
    bytes
        .get(args.offset..end)
        .ok_or_else(|| "Reference must contain the plaintext body at the selected offset".into())
}

pub(super) fn run(args: Args, json: bool) -> CliResult<u8> {
    let cipher = std::fs::read(&args.input)?;
    let (cipher, iv) = sample(&cipher, &args)?;
    let reference = args.reference.as_ref().map(std::fs::read).transpose()?;
    let reference = reference
        .as_ref()
        .map(|b| reference_sample(b, &args, cipher.len()))
        .transpose()?;
    let options = DecryptOptions {
        iv,
        chunk_size: args.chunk,
    };
    options.validate(cipher, reference)?;
    ensure_new_output(&args.output)?;
    let mut session = super::open_session(args.selector)?;
    let checks = session.check(|event| {
        let _ = write_check_event(&event, json, &mut io::stderr().lock());
    });
    if !checks.compatible {
        return print_report(&checks, json);
    }
    let mut progress = Progress::default();
    let output = session.decrypt(cipher, options, reference, |p| {
        let _ = progress.write(
            p.stage,
            p.completed as u64,
            p.total as u64,
            json,
            &mut io::stderr().lock(),
        );
        Ok(())
    })?;
    write_new_output(&args.output, &output.plaintext)?;
    if json {
        print_json(&serde_json::json!({
            "checks": checks,
            "decryption": output.report,
            "output": args.output,
        }))?;
    } else {
        let mut out = io::stdout().lock();
        writeln!(
            out,
            "Decrypted {} bytes in {:.3}s ({:.1} KiB/s)",
            output.report.bytes, output.report.seconds, output.report.kib_per_second
        )?;
        writeln!(
            out,
            "SHA-256: {}\nDFU cleanup: {:?}\nOutput: {}",
            output.report.sha256, output.report.cleanup, args.output
        )?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(s: &str) -> Vec<String> {
        s.split_whitespace().map(String::from).collect()
    }

    #[test]
    fn decrypt_arguments_fail_before_usb() {
        let valid = "--input cipher --output plain --size 528";
        assert!(parse(&args(valid)).is_ok());
        assert!(parse(&args("--input cipher --output plain")).is_ok());
        assert!(parse(&args("--input cipher --output plain --size 16777232")).is_ok());
        for suffix in [
            " --size 32",
            " --chunk 513",
            " --offset 1",
            " --offset 18446744073709551615",
            " --iv xyz",
            " --fresh-dfu",
            " --full",
            " --device 05ac:1223",
            " --skip-bytes 0 --body-offset 0",
        ] {
            assert!(
                parse(&args(&format!("{valid}{suffix}"))).is_err(),
                "{suffix}"
            );
        }
        for command in ["--input cipher --output plain --size 0", "--input cipher"] {
            assert!(parse(&args(command)).is_err());
        }
    }

    #[test]
    fn body_offsets_and_cbc_iv_are_unambiguous() {
        let mut parsed = parse(&args("--input cipher --output plain --size 16 --skip-bytes 0x800 --iv 01010101010101010101010101010101")).unwrap();
        let mut bytes = vec![0; 0x830];
        bytes[0x800..0x810].fill(0x23);
        assert_eq!(sample(&bytes, &parsed).unwrap().1, [1; 16]);
        parsed.offset = 16;
        assert_eq!(sample(&bytes, &parsed).unwrap().1, [0x23; 16]);
        parsed.offset = 48;
        assert!(sample(&bytes, &parsed).is_err());
    }

    #[test]
    fn whole_img1_uses_header_length_and_preserves_the_final_block() {
        let mut bytes = vec![0; 0x800 + 32 + 137];
        bytes[..7].copy_from_slice(b"87021.0");
        bytes[7] = 3;
        bytes[12..16].copy_from_slice(&24u32.to_le_bytes());
        bytes[0x800..0x810].fill(0x23);
        bytes[0x810..0x820].fill(0x45);
        bytes[0x820..].fill(0xcc);
        let mut parsed = parse(&args("--input osos.img1 --output plain")).unwrap();
        let (body, iv) = sample(&bytes, &parsed).unwrap();
        assert_eq!(body, &bytes[0x800..0x820]);
        assert_eq!(iv, [0; 16]);
        parsed.offset = 16;
        let (tail, iv) = sample(&bytes, &parsed).unwrap();
        assert_eq!(tail, &[0x45; 16]);
        assert_eq!(iv, [0x23; 16]);
        parsed.size = Some(32);
        assert!(sample(&bytes, &parsed).is_err()); // Footer cannot satisfy the range.
    }

    #[test]
    fn whole_raw_input_and_bounded_samples_validate_before_usb() {
        let mut parsed = parse(&args("--input cipher --output plain --skip-bytes 3")).unwrap();
        let bytes = vec![0x71; 3 + 65552];
        assert_eq!(sample(&bytes, &parsed).unwrap().0.len(), 65552);
        assert!(sample(&bytes[..bytes.len() - 1], &parsed).is_err());
        parsed.size = Some(16);
        assert_eq!(sample(&bytes, &parsed).unwrap().0.len(), 16);
        parsed.skip_bytes = Some(bytes.len() + 1);
        assert!(sample(&bytes, &parsed).is_err());
        parsed.skip_bytes = Some(bytes.len());
        parsed.size = None;
        assert!(sample(&bytes, &parsed).is_err());
        parsed.skip_bytes = None;
        assert!(sample(&bytes, &parsed).is_err());
    }

    #[test]
    fn reference_uses_plaintext_body_offsets() {
        let parsed = parse(&args("--input cipher --output plain --offset 16")).unwrap();
        let mut reference = vec![0; 32];
        reference[16..].fill(0x45);
        assert_eq!(
            reference_sample(&reference, &parsed, 16).unwrap(),
            &[0x45; 16]
        );
        assert!(reference_sample(&reference, &parsed, 32).is_err());
    }
}
