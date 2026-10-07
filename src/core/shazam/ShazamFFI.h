#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
char* generate_shazam_signature(const char* file_path);
void free_shazam_string(char* s);
#else
inline char* generate_shazam_signature(const char* file_path) {
    (void)file_path;
    return nullptr;
}
inline void free_shazam_string(char* s) {
    (void)s;
}
#endif

#ifdef __cplusplus
}
#endif