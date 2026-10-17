#include <stdbool.h>
#include <stddef.h>

typedef enum {
    Vdf_Result_Changed,
    Vdf_Result_Unchanged,
    Vdf_Result_Unrecognized,
} VdfResult;

char *vdf_set_launch_options(const char *text, const char *app_id, const char *launch_options, VdfResult *result);
bool vdf_get_launch_options(const char *text, const char *app_id, char *out_value, size_t out_value_size);
