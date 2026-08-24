use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use shazamio_core::fingerprinting::algorithm::SignatureGenerator;
use shazamio_core::utils::unwrap_decoded_signature;

#[no_mangle]
pub extern "C" fn generate_shazam_signature(file_path: *const c_char) -> *mut c_char {
    if file_path.is_null() {
        return std::ptr::null_mut();
    }

    let c_str = unsafe { CStr::from_ptr(file_path) };
    let path = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    // Вызываем чистое DSP-ядро напрямую (12 секунд - стандарт Shazam)
    let data = match SignatureGenerator::make_signature_from_file(path, Some(12)) {
        Ok(d) => d,
        Err(_) => return std::ptr::null_mut(),
    };

    let signature = match unwrap_decoded_signature(data) {
        Ok(sig) => sig,
        Err(_) => return std::ptr::null_mut(),
    };

    match CString::new(signature.signature.uri) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn free_shazam_string(s: *mut c_char) {
    if s.is_null() { return; }
    unsafe {
        let _ = CString::from_raw(s);
    }
}