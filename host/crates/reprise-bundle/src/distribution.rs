// SPDX-License-Identifier: GPL-3.0-only

use crate::{
    asset_filename, authenticate, check_asset, invalid, sha256, valid_hash, write_new, Result,
    TrustedKey, VerifiedBundle, MANIFEST_FILE, MAX_MANIFEST_BYTES, SIGNATURE_FILE,
};
use reqwest::{blocking::Client, redirect::Policy, Url};
use std::{
    collections::BTreeMap,
    fs,
    io::Read,
    path::{Path, PathBuf},
    time::Duration,
};

/// Fetch and verify a bundle over HTTPS, reusing the cache when its digest is supplied.
pub fn fetch(
    manifest_url: &str,
    expected_digest: Option<&str>,
    key: &TrustedKey,
    cache: &Path,
    installer_version: &str,
) -> Result<(PathBuf, VerifiedBundle)> {
    let url = manifest_url_checked(manifest_url)?;
    let client = Client::builder()
        .https_only(true)
        .redirect(Policy::limited(5))
        .connect_timeout(Duration::from_secs(15))
        .timeout(Duration::from_secs(120))
        .user_agent(concat!("reprise-bundle/", env!("CARGO_PKG_VERSION")))
        .build()?;
    fetch_with(
        &url,
        expected_digest,
        key,
        cache,
        installer_version,
        |url, limit| {
            let response = client
                .get(url.clone())
                .header("Accept-Encoding", "identity")
                .send()?
                .error_for_status()?;
            if response.content_length().is_some_and(|n| n > limit as u64) {
                return Err(invalid("Download exceeds size limit"));
            }
            let mut bytes = Vec::new();
            response.take(limit as u64 + 1).read_to_end(&mut bytes)?;
            if bytes.len() > limit {
                return Err(invalid("Download exceeds size limit"));
            }
            Ok(bytes)
        },
    )
}

fn manifest_url_checked(value: &str) -> Result<Url> {
    let url = Url::parse(value).map_err(|_| invalid("Invalid bundle URL"))?;
    if url.scheme() != "https"
        || url.host_str().is_none()
        || !url.username().is_empty()
        || url.password().is_some()
        || url.query().is_some()
        || url.fragment().is_some()
        || !url.path().ends_with("/manifest.json")
    {
        return Err(invalid(
            "Bundle URL must be an HTTPS manifest.json URL without credentials, query or fragment",
        ));
    }
    Ok(url)
}

fn fetch_with(
    url: &Url,
    expected_digest: Option<&str>,
    key: &TrustedKey,
    cache: &Path,
    installer_version: &str,
    mut get: impl FnMut(&Url, usize) -> Result<Vec<u8>>,
) -> Result<(PathBuf, VerifiedBundle)> {
    if expected_digest.is_some_and(|h| !valid_hash(h)) {
        return Err(invalid("Invalid pinned manifest SHA-256"));
    }
    if let Some(hash) = expected_digest {
        let directory = cache.join(hash);
        if directory.try_exists()? {
            let bundle = VerifiedBundle::load(&directory, key, installer_version)?;
            if bundle.digest() != hash {
                return Err(invalid("Cached manifest digest mismatch"));
            }
            return Ok((directory, bundle));
        }
    }
    let raw = get(url, MAX_MANIFEST_BYTES)?;
    let digest = sha256(&raw);
    if expected_digest.is_some_and(|h| h != digest) {
        return Err(invalid("Downloaded manifest does not match pinned SHA-256"));
    }
    let sibling = |name: &str| {
        url.join(name)
            .map_err(|_| invalid("Invalid release asset URL"))
    };
    let signature = get(&sibling(SIGNATURE_FILE)?, 64)?;
    let manifest = authenticate(&raw, &signature, key, installer_version)?;
    let directory = cache.join(&digest);
    if directory.try_exists()? {
        let bundle = VerifiedBundle::load(&directory, key, installer_version)?;
        if bundle.digest() != digest {
            return Err(invalid("Cached manifest digest mismatch"));
        }
        return Ok((directory, bundle));
    }
    fs::create_dir_all(cache)?;
    let staging = tempfile::Builder::new()
        .prefix(".download-")
        .tempdir_in(cache)?;
    write_new(&staging.path().join(MANIFEST_FILE), &raw)?;
    write_new(&staging.path().join(SIGNATURE_FILE), &signature)?;
    let mut assets = BTreeMap::new();
    for (hash, asset) in &manifest.assets {
        let bytes = get(&sibling(&asset_filename(hash))?, asset.bytes)?;
        check_asset(hash, asset, &bytes)?;
        write_new(&staging.path().join(asset_filename(hash)), &bytes)?;
        assets.insert(hash.clone(), bytes);
    }
    let bundle = VerifiedBundle {
        manifest,
        digest: digest.clone(),
        assets,
    };
    match fs::rename(staging.path(), &directory) {
        Ok(()) => Ok((directory, bundle)),
        Err(_) if directory.try_exists()? => {
            let winner = VerifiedBundle::load(&directory, key, installer_version)?;
            if winner.digest() != digest {
                return Err(invalid("Concurrent cache digest mismatch"));
            }
            Ok((directory, winner))
        }
        Err(error) => Err(error.into()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{read_file, tests::Fixture, MAX_ASSET_BYTES};

    #[test]
    fn reject_unsafe_urls() {
        for url in [
            "http://example.org/manifest.json",
            "https://user@example.org/manifest.json",
            "https://example.org/manifest.json?key=1",
            "https://example.org/manifest.json#x",
            "https://example.org/other.json",
            "file:///manifest.json",
        ] {
            assert!(manifest_url_checked(url).is_err(), "{url}");
        }
        assert!(manifest_url_checked(
            "https://github.com/owner/repo/releases/download/v1/manifest.json"
        )
        .is_ok());
    }

    #[test]
    fn authentication_finishes_before_any_asset_download() {
        let fixture = Fixture::new();
        let url = manifest_url_checked("https://example.org/v1/manifest.json").unwrap();
        let cache = tempfile::tempdir().unwrap();
        let mut requested = Vec::new();
        let result = fetch_with(
            &url,
            None,
            &fixture.key,
            cache.path(),
            "0.1.0",
            |url, limit| {
                let name = url.path_segments().unwrap().next_back().unwrap();
                requested.push(name.to_owned());
                assert!(!name.ends_with(".blob"));
                let mut bytes = read_file(&fixture.dir.path().join(name), limit)?;
                if name == SIGNATURE_FILE {
                    bytes[0] ^= 1;
                }
                Ok(bytes)
            },
        );
        assert!(result.is_err());
        assert_eq!(requested, [MANIFEST_FILE, SIGNATURE_FILE]);
        assert_eq!(fs::read_dir(cache.path()).unwrap().count(), 0);
    }

    #[test]
    fn download_cache_revalidation_pinning_and_interruption() {
        let fixture = Fixture::new();
        let url = manifest_url_checked("https://example.org/releases/v1/manifest.json").unwrap();
        let cache = tempfile::tempdir().unwrap();
        let get = |url: &Url, _: usize| {
            read_file(
                &fixture
                    .dir
                    .path()
                    .join(url.path_segments().unwrap().next_back().unwrap()),
                MAX_ASSET_BYTES,
            )
        };
        let (path, bundle) =
            fetch_with(&url, None, &fixture.key, cache.path(), "0.1.0", get).unwrap();
        assert_eq!(path.file_name().unwrap(), bundle.digest());
        fetch_with(
            &url,
            Some(bundle.digest()),
            &fixture.key,
            cache.path(),
            "0.1.0",
            |_, _| panic!("cache should be offline"),
        )
        .unwrap();
        let digest = bundle.digest().to_owned();
        let hash = bundle.manifest().assets.keys().next().unwrap();
        fs::write(path.join(asset_filename(hash)), b"corrupt").unwrap();
        assert!(fetch_with(
            &url,
            Some(&digest),
            &fixture.key,
            cache.path(),
            "0.1.0",
            |_, _| panic!("corrupt cache must fail closed")
        )
        .is_err());

        let cache = tempfile::tempdir().unwrap();
        assert!(fetch_with(
            &url,
            Some(&"0".repeat(64)),
            &fixture.key,
            cache.path(),
            "0.1.0",
            get
        )
        .is_err());
        let result = fetch_with(&url, None, &fixture.key, cache.path(), "0.1.0", |url, n| {
            if url.path().ends_with(".blob") {
                Err(invalid("disconnected"))
            } else {
                get(url, n)
            }
        });
        assert!(result.is_err());
        assert_eq!(fs::read_dir(cache.path()).unwrap().count(), 0);
    }
}
