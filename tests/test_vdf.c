#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../installer/vdf.h"

static const char *const config_with_app =
    "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n"
    "\t\t\t\t\"apps\"\n\t\t\t\t{\n"
    "\t\t\t\t\t\"252330\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"Playtime\"\t\t\"32\"\n\t\t\t\t\t}\n"
    "\t\t\t\t\t\"311210\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1790728722\"\n"
    "\t\t\t\t\t\t\"cloud\"\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\t\"last_sync_state\"\t\t\"synchronized\"\n\t\t\t\t\t\t}\n"
    "\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n"
    "\t\"Other\"\n\t{\n\t\t\"311210\"\t\t\"not an app block\"\n\t}\n}\n";

static const char *const config_without_app =
    "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n"
    "\t\t\t\t\"apps\"\n\t\t\t\t{\n"
    "\t\t\t\t\t\"252330\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"Playtime\"\t\t\"32\"\n\t\t\t\t\t}\n"
    "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";

static const char *const launch_options = "\"/Users/a b/Call of Duty Black Ops III/BO3MacFix/launch-bo3macfix\" %command%";

static void test_adds_option_to_existing_app_block(void) {
    VdfResult result;
    char *updated = vdf_set_launch_options(config_with_app, "311210", launch_options, &result);
    assert(result == Vdf_Result_Changed);
    char value[512];
    assert(vdf_get_launch_options(updated, "311210", value, sizeof(value)));
    assert(strcmp(value, launch_options) == 0);
    assert(strstr(updated, "\t\t\t\t\t\t\"LaunchOptions\"\t\t\"\\\"/Users/a b/") != NULL);
    assert(!vdf_get_launch_options(updated, "252330", value, sizeof(value)));
    free(updated);
}

static void test_replaces_existing_option(void) {
    VdfResult result;
    char *first = vdf_set_launch_options(config_with_app, "311210", "old %command%", &result);
    char *second = vdf_set_launch_options(first, "311210", launch_options, &result);
    assert(result == Vdf_Result_Changed);
    assert(strstr(second, "old %command%") == NULL);
    char value[512];
    assert(vdf_get_launch_options(second, "311210", value, sizeof(value)));
    assert(strcmp(value, launch_options) == 0);
    free(first);
    free(second);
}

static void test_removes_option_and_restores_text(void) {
    VdfResult result;
    char *added = vdf_set_launch_options(config_with_app, "311210", launch_options, &result);
    char *removed = vdf_set_launch_options(added, "311210", NULL, &result);
    assert(result == Vdf_Result_Changed);
    assert(strcmp(removed, config_with_app) == 0);
    char *again = vdf_set_launch_options(removed, "311210", NULL, &result);
    assert(result == Vdf_Result_Unchanged);
    free(added);
    free(removed);
    free(again);
}

static void test_creates_app_block_when_missing(void) {
    VdfResult result;
    char *updated = vdf_set_launch_options(config_without_app, "311210", launch_options, &result);
    assert(result == Vdf_Result_Changed);
    char value[512];
    assert(vdf_get_launch_options(updated, "311210", value, sizeof(value)));
    assert(strcmp(value, launch_options) == 0);
    free(updated);
}

static void test_rejects_unrecognized_text(void) {
    VdfResult result;
    assert(vdf_set_launch_options("\"broken\"\n{\n", "311210", launch_options, &result) == NULL);
    assert(result == Vdf_Result_Unrecognized);
    assert(vdf_set_launch_options("\"UserLocalConfigStore\"\n{\n}\n", "311210", launch_options, &result) == NULL);
    assert(result == Vdf_Result_Unrecognized);
}

int main(void) {
    test_adds_option_to_existing_app_block();
    test_replaces_existing_option();
    test_removes_option_and_restores_text();
    test_creates_app_block_when_missing();
    test_rejects_unrecognized_text();
    puts("vdf tests passed");
    return 0;
}
