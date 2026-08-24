#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char* generate_shazam_signature(const char* file_path);
void free_shazam_string(char* s);

#ifdef __cplusplus
}
#endif