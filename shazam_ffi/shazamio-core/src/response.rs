use pyo3::{pyclass, pymethods, PyResult};
use serde::{Deserialize, Serialize};

#[derive(Clone, Serialize, Deserialize)]
#[pyclass(from_py_object)]
pub struct Geolocation {
    #[pyo3(get)]
    pub altitude: i16,
    #[pyo3(get)]
    pub latitude: i8,
    #[pyo3(get)]
    pub longitude: i8,
}

#[derive(Clone, Serialize, Deserialize)]
#[pyclass(from_py_object)]
pub struct SignatureSong {
    #[pyo3(get)]
    pub samples: u32,
    #[pyo3(get)]
    pub timestamp: u32,
    #[pyo3(get)]
    pub uri: String,
}

#[derive(Clone, Serialize, Deserialize)]
#[pyclass(from_py_object)]
pub struct Signature {
    #[pyo3(get)]
    pub geolocation: Geolocation,
    #[pyo3(get)]
    pub signature: SignatureSong,
    #[pyo3(get)]
    pub timestamp: u32,
    #[pyo3(get)]
    pub timezone: String,
}

#[pymethods]
impl Geolocation {
    #[new]
    pub fn new(altitude: i16, latitude: i8, longitude: i8) -> PyResult<Self> {
        Ok(Geolocation {
            altitude,
            latitude,
            longitude,
        })
    }
}

#[pymethods]
impl SignatureSong {
    #[new]
    pub fn new(samples: u32, timestamp: u32, uri: String) -> PyResult<Self> {
        Ok(SignatureSong {
            samples,
            timestamp,
            uri,
        })
    }
}

#[pymethods]
impl Signature {
    #[new]
    pub fn new(
        geolocation: Geolocation,
        signature: SignatureSong,
        timestamp: u32,
        timezone: String,
    ) -> PyResult<Self> {
        Ok(Signature {
            geolocation,
            signature,
            timestamp,
            timezone,
        })
    }
}
