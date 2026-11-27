// SPDX-License-Identifier: GPL-3.0-only

//! Keeps macOS from moving an iPod out of BootROM DFU.
//!
//! When a Classic enters DFU, `AMPDevicesAgent` uploads Apple's WTF image and
//! the iPod re-enumerates in WTF recovery, where the installer cannot use it.
//! The agent skips DFU and recovery devices while the `ignore-devices` user
//! default is true in its own domain; `AMPDeviceDiscoveryAgent` reads the same
//! key and then hides the device from Finder. The agent checks the key for
//! other connected Apple devices too; a Classic in disk mode is unaffected.
//!
//! The guard sets the key in both domains and puts the previous values back.
//! Before touching the keys it writes them to a marker file under the
//! installer's data directory, and it holds an exclusive lock on a separate
//! lock file while the keys are set. The kernel drops that lock when the
//! process dies, so a marker whose lock is free belongs to a session that
//! ended without restoring, and [`recover_stale`] restores it. A restore only
//! touches a key that is still exactly `true`, so a value someone changed
//! during the session stays.

use crate::Result;

/// Holds the keys set until dropped. Only one guard exists per user at a time.
#[must_use]
pub struct MacDfuGuard(());

impl MacDfuGuard {
    /// Set the keys, or fail with [`crate::Error::OtherInstaller`] when another
    /// guard holds them.
    pub fn acquire() -> Result<Self> {
        imp::acquire(false)?;
        Ok(Self(()))
    }

    /// Like [`Self::acquire`], for a process that only holds the guard, so
    /// that DFU commands may run under it; see [`standalone_running`].
    pub fn acquire_standalone() -> Result<Self> {
        imp::acquire(true)?;
        Ok(Self(()))
    }
}

impl Drop for MacDfuGuard {
    fn drop(&mut self) {
        // A failed restore leaves the marker, so the next launch restores.
        let _ = restore_now();
    }
}

/// Restore the keys held by this process, if any. Safe to call more than once
/// and from a panic hook, which runs before an abort ends the process.
pub fn restore_now() -> Result<()> {
    imp::restore_now()
}

/// Restore what an earlier session left set. Returns whether there was
/// anything to restore. Leaves a live session's keys alone.
pub fn recover_stale() -> Result<bool> {
    imp::recover_stale()
}

/// Whether this process holds the keys. A panic restores them while the
/// guard value lives on, which only an unwinding build survives.
pub fn held() -> bool {
    imp::held()
}

/// Whether another process holds a standalone guard and the keys are set.
pub fn standalone_running() -> bool {
    imp::standalone_running()
}

/// Restore the keys when the process panics, then run the previous hook.
pub fn install_panic_hook() {
    let previous = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        imp::restore_from_panic();
        previous(info);
    }));
}

#[cfg(not(target_os = "macos"))]
mod imp {
    use crate::Result;

    pub(super) fn acquire(_standalone: bool) -> Result<()> {
        Ok(())
    }
    pub(super) fn restore_now() -> Result<()> {
        Ok(())
    }
    pub(super) fn recover_stale() -> Result<bool> {
        Ok(false)
    }
    pub(super) fn held() -> bool {
        false
    }
    pub(super) fn standalone_running() -> bool {
        false
    }
    pub(super) fn restore_from_panic() {}
}

#[cfg(target_os = "macos")]
mod imp {
    use crate::{invalid, Error, Result};
    use core_foundation::{
        base::{CFType, TCFType},
        boolean::CFBoolean,
        data::CFData,
        dictionary::CFDictionary,
        propertylist::{self, CFPropertyList},
        string::CFString,
    };
    use core_foundation_sys::preferences::{
        kCFPreferencesAnyHost, kCFPreferencesCurrentUser, CFPreferencesCopyValue,
        CFPreferencesSetValue, CFPreferencesSynchronize,
    };
    use std::{
        fs::{self, File, OpenOptions},
        io::{self, Read, Write},
        os::fd::AsRawFd,
        path::{Path, PathBuf},
        sync::Mutex,
    };

    const KEY: &str = "ignore-devices";
    const DOMAINS: [&str; 2] = [
        "com.apple.AMPDevicesAgent",
        "com.apple.AMPDeviceDiscoveryAgent",
    ];
    /// The desktop app's Tauri identifier, so the CLI and the app share files.
    const DATA_DIRECTORY: &str = "Library/Application Support/com.lukasgreicius.repriseos.installer";
    /// Lock file contents while a standalone guard holds the lock.
    const STANDALONE: &[u8] = b"standalone";

    pub(super) struct Store {
        pub(super) directory: PathBuf,
        pub(super) domains: Vec<String>,
    }

    /// An exclusive lock on the store's lock file, released on drop.
    pub(super) struct Held {
        store: Store,
        _lock: File,
    }

    static HELD: Mutex<Option<Held>> = Mutex::new(None);

    pub(super) fn acquire(standalone: bool) -> Result<()> {
        let mut held = HELD.lock().unwrap_or_else(|e| e.into_inner());
        if held.is_some() {
            return Err(Error::OtherInstaller);
        }
        *held = Some(begin(Store::user()?, standalone)?);
        Ok(())
    }

    pub(super) fn restore_now() -> Result<()> {
        // Holding the mutex through the restore makes a signal thread that
        // calls this wait for a restore already running.
        let mut held = HELD.lock().unwrap_or_else(|e| e.into_inner());
        held.take().map_or(Ok(()), finish)
    }

    pub(super) fn restore_from_panic() {
        // A panic inside restore_now holds the mutex; that restore failed and
        // the marker stays for the next launch.
        let held = match HELD.try_lock() {
            Ok(mut held) => held.take(),
            Err(std::sync::TryLockError::Poisoned(e)) => e.into_inner().take(),
            Err(std::sync::TryLockError::WouldBlock) => None,
        };
        if let Some(held) = held {
            let _ = finish(held);
        }
    }

    pub(super) fn held() -> bool {
        HELD.lock().unwrap_or_else(|e| e.into_inner()).is_some()
    }

    pub(super) fn recover_stale() -> Result<bool> {
        recover(Store::user()?)
    }

    pub(super) fn standalone_running() -> bool {
        Store::user().is_ok_and(|store| store_standalone_running(&store))
    }

    impl Store {
        fn user() -> Result<Self> {
            let home = std::env::var_os("HOME").ok_or_else(|| invalid("HOME is not set"))?;
            Ok(Self {
                directory: Path::new(&home).join(DATA_DIRECTORY),
                domains: DOMAINS.iter().map(|d| d.to_string()).collect(),
            })
        }

        fn marker(&self) -> PathBuf {
            self.directory.join("mac-dfu-guard.plist")
        }

        fn lock_path(&self) -> PathBuf {
            self.directory.join("mac-dfu-guard.lock")
        }

        /// Take the lock, or `None` when another open file holds it.
        fn lock(&self) -> Result<Option<File>> {
            fs::create_dir_all(&self.directory).map_err(|e| io_error("data directory", e))?;
            let file = OpenOptions::new()
                .read(true)
                .write(true)
                .create(true)
                .truncate(false)
                .open(self.lock_path())
                .map_err(|e| io_error("lock file", e))?;
            // SAFETY: flock on a descriptor this function owns.
            if unsafe { libc::flock(file.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } == 0 {
                return Ok(Some(file));
            }
            let error = io::Error::last_os_error();
            if error.raw_os_error() == Some(libc::EWOULDBLOCK) {
                Ok(None)
            } else {
                Err(io_error("lock file", error))
            }
        }

        /// The recorded values by domain; a domain without an entry had no
        /// key. An unreadable marker counts as no keys, the agents' default.
        fn read_marker(&self) -> Result<Option<CFDictionary<CFString, CFType>>> {
            let bytes = match fs::read(self.marker()) {
                Ok(bytes) => bytes,
                Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
                Err(e) => return Err(io_error("marker", e)),
            };
            let parsed = propertylist::create_with_data(CFData::from_buffer(&bytes), 0)
                .ok()
                .map(|(list, _)| unsafe { CFPropertyList::wrap_under_create_rule(list) })
                .and_then(|list| list.downcast_into::<CFDictionary>());
            Ok(Some(match parsed {
                Some(dictionary) => unsafe {
                    CFDictionary::wrap_under_get_rule(dictionary.as_concrete_TypeRef())
                },
                None => CFDictionary::from_CFType_pairs(&[]),
            }))
        }

        fn write_marker(&self, values: &[(CFString, CFType)]) -> Result<()> {
            let dictionary = CFDictionary::from_CFType_pairs(values);
            let data = propertylist::create_data(
                dictionary.as_CFTypeRef(),
                propertylist::kCFPropertyListXMLFormat_v1_0,
            )
            .map_err(|e| invalid(format!("marker: {e}")))?;
            let temporary = self.directory.join("mac-dfu-guard.plist.tmp");
            let mut file = File::create(&temporary).map_err(|e| io_error("marker", e))?;
            file.write_all(data.bytes())
                .and_then(|()| file.sync_all())
                .and_then(|()| fs::rename(&temporary, self.marker()))
                .map_err(|e| io_error("marker", e))?;
            // Directory fsync makes the rename durable; not every file system
            // supports it, and the marker is already written either way.
            let _ = File::open(&self.directory).and_then(|d| d.sync_all());
            Ok(())
        }
    }

    pub(super) fn begin(store: Store, standalone: bool) -> Result<Held> {
        let mut lock = store.lock()?.ok_or(Error::OtherInstaller)?;
        lock.set_len(0)
            .and_then(|()| lock.write_all(if standalone { STANDALONE } else { b"" }))
            .map_err(|e| io_error("lock file", e))?;
        // A marker without a holder means a session died with the keys set:
        // its values are the originals, the current ones are its own.
        if store.read_marker()?.is_none() {
            let values: Vec<_> = store
                .domains
                .iter()
                .filter_map(|domain| Some((CFString::new(domain), copy_value(domain)?)))
                .collect();
            store.write_marker(&values)?;
        }
        let set = CFBoolean::true_value().as_CFType();
        if let Err(error) = store
            .domains
            .iter()
            .try_for_each(|domain| set_value(domain, Some(&set)))
        {
            let _ = restore(&store);
            return Err(error);
        }
        Ok(Held { store, _lock: lock })
    }

    pub(super) fn finish(held: Held) -> Result<()> {
        restore(&held.store)
    }

    pub(super) fn recover(store: Store) -> Result<bool> {
        if !store.marker().exists() {
            return Ok(false);
        }
        let Some(_lock) = store.lock()? else {
            return Ok(false);
        };
        if store.read_marker()?.is_none() {
            return Ok(false);
        }
        restore(&store)?;
        Ok(true)
    }

    pub(super) fn store_standalone_running(store: &Store) -> bool {
        let mut contents = Vec::new();
        matches!(store.lock(), Ok(None))
            && File::open(store.lock_path())
                .and_then(|mut f| f.read_to_end(&mut contents))
                .is_ok()
            && contents == STANDALONE
            && store.marker().exists()
            && store.domains.iter().all(|d| is_set(copy_value(d).as_ref()))
    }

    /// Put back the recorded values, then delete the marker. Needs the lock.
    /// A failed write keeps the marker for the next launch.
    fn restore(store: &Store) -> Result<()> {
        let Some(recorded) = store.read_marker()? else {
            return Ok(());
        };
        let mut result = Ok(());
        for domain in &store.domains {
            if is_set(copy_value(domain).as_ref()) {
                let original = recorded.find(CFString::new(domain));
                if let Err(error) = set_value(domain, original.as_deref()) {
                    result = Err(error);
                }
            }
        }
        result?;
        match fs::remove_file(store.marker()) {
            Err(e) if e.kind() != io::ErrorKind::NotFound => Err(io_error("marker", e)),
            _ => Ok(()),
        }
    }

    /// Only the exact value the guard writes counts; `defaults write -int 1`
    /// or `"YES"` came from someone else.
    fn is_set(value: Option<&CFType>) -> bool {
        value
            .and_then(|v| v.downcast::<CFBoolean>())
            .is_some_and(bool::from)
    }

    /// The user's own value, without ByHost, global or managed values merged in.
    fn copy_value(domain: &str) -> Option<CFType> {
        let key = CFString::new(KEY);
        let domain = CFString::new(domain);
        // SAFETY: CF objects outlive the call; the result follows the create rule.
        unsafe {
            let value = CFPreferencesCopyValue(
                key.as_concrete_TypeRef(),
                domain.as_concrete_TypeRef(),
                kCFPreferencesCurrentUser,
                kCFPreferencesAnyHost,
            );
            (!value.is_null()).then(|| CFType::wrap_under_create_rule(value))
        }
    }

    /// Write or, with `None`, delete the key, then flush it for the agents.
    fn set_value(domain: &str, value: Option<&CFType>) -> Result<()> {
        let key = CFString::new(KEY);
        let name = CFString::new(domain);
        // SAFETY: CF objects outlive the calls; a null value deletes the key.
        let flushed = unsafe {
            CFPreferencesSetValue(
                key.as_concrete_TypeRef(),
                value.map_or(std::ptr::null(), |v| v.as_CFTypeRef()),
                name.as_concrete_TypeRef(),
                kCFPreferencesCurrentUser,
                kCFPreferencesAnyHost,
            );
            CFPreferencesSynchronize(
                name.as_concrete_TypeRef(),
                kCFPreferencesCurrentUser,
                kCFPreferencesAnyHost,
            )
        };
        if flushed == 0 {
            return Err(invalid(format!(
                "macOS DFU guard: could not write {domain}"
            )));
        }
        Ok(())
    }

    fn io_error(what: &str, error: io::Error) -> Error {
        invalid(format!("macOS DFU guard {what}: {error}"))
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::sync::atomic::{AtomicU32, Ordering};

        /// The tests share one domain in the user's preferences store; run
        /// them one at a time.
        static SERIAL: Mutex<()> = Mutex::new(());
        const TEST_DOMAIN: &str = "com.lukasgreicius.repriseos.installer.test";

        struct Scratch {
            store_directory: PathBuf,
            domain: String,
        }

        impl Scratch {
            fn new() -> Self {
                static NEXT: AtomicU32 = AtomicU32::new(0);
                let n = NEXT.fetch_add(1, Ordering::Relaxed);
                let id = format!("{}-{n}", std::process::id());
                let store_directory = std::env::temp_dir().join(format!("mac-dfu-guard-{id}"));
                let scratch = Self {
                    store_directory,
                    domain: TEST_DOMAIN.to_string(),
                };
                set_value(&scratch.domain, None).unwrap();
                scratch
            }

            fn store(&self) -> Store {
                Store {
                    directory: self.store_directory.clone(),
                    domains: vec![self.domain.clone()],
                }
            }

            fn set(&self, value: Option<CFType>) {
                set_value(&self.domain, value.as_ref()).unwrap();
            }

            fn value(&self) -> Option<CFType> {
                copy_value(&self.domain)
            }
        }

        impl Drop for Scratch {
            fn drop(&mut self) {
                let _ = set_value(&self.domain, None);
                let _ = fs::remove_dir_all(&self.store_directory);
                if let Some(home) = std::env::var_os("HOME") {
                    let file = format!("Library/Preferences/{}.plist", self.domain);
                    let _ = fs::remove_file(Path::new(&home).join(file));
                }
            }
        }

        fn boolean(value: bool) -> CFType {
            CFBoolean::from(value).as_CFType()
        }

        fn same(a: Option<CFType>, b: Option<CFType>) -> bool {
            match (a, b) {
                (None, None) => true,
                (Some(a), Some(b)) => a == b,
                _ => false,
            }
        }

        fn round_trip(original: Option<CFType>) {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            scratch.set(original.clone());
            let held = begin(scratch.store(), false).unwrap();
            assert!(is_set(scratch.value().as_ref()));
            finish(held).unwrap();
            assert!(same(scratch.value(), original));
            assert!(!scratch.store().marker().exists());
        }

        #[test]
        fn restores_a_missing_key() {
            round_trip(None);
        }

        #[test]
        fn restores_false() {
            round_trip(Some(boolean(false)));
        }

        #[test]
        fn restores_a_non_boolean_value() {
            round_trip(Some(CFString::new("NO").as_CFType()));
        }

        #[test]
        fn keeps_true_that_was_there_before() {
            round_trip(Some(boolean(true)));
        }

        #[test]
        fn keeps_a_value_changed_during_the_session() {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            let held = begin(scratch.store(), false).unwrap();
            scratch.set(Some(boolean(false)));
            finish(held).unwrap();
            assert!(same(scratch.value(), Some(boolean(false))));
        }

        #[test]
        fn refuses_a_second_guard() {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            let held = begin(scratch.store(), false).unwrap();
            assert!(matches!(
                begin(scratch.store(), false),
                Err(Error::OtherInstaller)
            ));
            assert!(!recover(scratch.store()).unwrap());
            assert!(!store_standalone_running(&scratch.store()));
            finish(held).unwrap();
        }

        #[test]
        fn tells_a_standalone_guard_apart() {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            let held = begin(scratch.store(), true).unwrap();
            assert!(store_standalone_running(&scratch.store()));
            finish(held).unwrap();
            assert!(!store_standalone_running(&scratch.store()));
        }

        #[test]
        fn recovers_after_a_session_died() {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            scratch.set(Some(boolean(false)));
            // Dropping the lock without restoring is what a killed process leaves.
            drop(begin(scratch.store(), false).unwrap());
            assert!(is_set(scratch.value().as_ref()));
            assert!(recover(scratch.store()).unwrap());
            assert!(same(scratch.value(), Some(boolean(false))));
            assert!(!recover(scratch.store()).unwrap());
        }

        #[test]
        fn keeps_the_originals_of_a_dead_session() {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            drop(begin(scratch.store(), false).unwrap());
            let held = begin(scratch.store(), false).unwrap();
            finish(held).unwrap();
            assert!(scratch.value().is_none());
        }

        #[test]
        fn restoring_twice_is_harmless() {
            let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
            let scratch = Scratch::new();
            let held = begin(scratch.store(), false).unwrap();
            restore(&scratch.store()).unwrap();
            scratch.set(Some(boolean(true)));
            finish(held).unwrap();
            assert!(is_set(scratch.value().as_ref()));
        }
    }
}
