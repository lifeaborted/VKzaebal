use std::error::Error;
use std::time::SystemTime;

use crate::fingerprinting::signature_format::DecodedSignature;

#[derive(Debug)]
pub struct GeolocationResponse {
    pub altitude: i16,
    pub latitude: i8,
    pub longitude: i8,
}

#[derive(Debug)]
pub struct SignatureSong {
    pub samples: u32,
    pub timestamp: u32,
    pub uri: String,
}

#[derive(Debug)]
pub struct Signature {
    pub geolocation: GeolocationResponse,
    pub signature: SignatureSong,
    pub timestamp: u32,
    pub timezone: String,
}

pub fn get_signature_json(signature: &DecodedSignature) -> Result<Signature, Box<dyn Error>> {
    let timestamp_ms = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)?
        .as_millis();
    let samples =
        (signature.number_samples as f32 / signature.sample_rate_hz as f32 * 1000.) as u32;
    Ok(Signature {
        geolocation: GeolocationResponse {
            altitude: 300,
            latitude: 45,
            longitude: 2,
        },
        signature: SignatureSong {
            samples,
            timestamp: timestamp_ms as u32,
            uri: signature.encode_to_uri()?,
        },
        timestamp: timestamp_ms as u32,
        timezone: "Europe/Paris".to_string(),
    })
}
