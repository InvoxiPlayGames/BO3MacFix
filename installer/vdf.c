#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "vdf.h"

#define Vdf_Max_Depth 32

typedef enum {
    Token_End,
    Token_String,
    Token_Open,
    Token_Close,
    Token_Invalid,
} TokenType;

typedef struct {
    TokenType type;
    size_t start;
    size_t end;
} Token;

typedef struct {
    const char *text;
    size_t length;
    size_t position;
} Lexer;

static void skip_space_and_comments(Lexer *lexer) {
    while (lexer->position < lexer->length) {
        char c = lexer->text[lexer->position];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lexer->position++;
        } else if (c == '/' && lexer->position + 1 < lexer->length && lexer->text[lexer->position + 1] == '/') {
            while (lexer->position < lexer->length && lexer->text[lexer->position] != '\n')
                lexer->position++;
        } else {
            return;
        }
    }
}

static Token next_token(Lexer *lexer) {
    skip_space_and_comments(lexer);
    Token token = { Token_End, lexer->position, lexer->position };
    if (lexer->position >= lexer->length)
        return token;
    char c = lexer->text[lexer->position];
    if (c == '{' || c == '}') {
        token.type = c == '{' ? Token_Open : Token_Close;
        token.end = ++lexer->position;
        return token;
    }
    if (c != '"') {
        token.type = Token_Invalid;
        return token;
    }
    lexer->position++;
    while (lexer->position < lexer->length && lexer->text[lexer->position] != '"') {
        if (lexer->text[lexer->position] == '\\')
            lexer->position++;
        lexer->position++;
    }
    if (lexer->position >= lexer->length) {
        token.type = Token_Invalid;
        return token;
    }
    token.type = Token_String;
    token.end = ++lexer->position;
    return token;
}

static bool token_equals(const char *text, Token token, const char *expected) {
    size_t inner_length = token.end - token.start - 2;
    return inner_length == strlen(expected) && strncasecmp(text + token.start + 1, expected, inner_length) == 0;
}

typedef struct {
    size_t open_end;
    size_t close_start;
    bool found;
} Block;

typedef struct {
    Block apps;
    Block app;
    Token launch_options_key;
    Token launch_options_value;
    bool has_launch_options;
} AppLocation;

static const char *const apps_path[] = { "UserLocalConfigStore", "Software", "Valve", "Steam", "apps" };
#define Apps_Path_Depth 5

static bool locate_app(const char *text, size_t length, const char *app_id, AppLocation *location) {
    memset(location, 0, sizeof(*location));
    Lexer lexer = { text, length, 0 };
    Token names[Vdf_Max_Depth];
    size_t open_ends[Vdf_Max_Depth];
    int depth = 0;
    Token pending_key = { Token_End, 0, 0 };
    bool has_pending_key = false;

    for (;;) {
        Token token = next_token(&lexer);
        if (token.type == Token_End)
            return depth == 0;
        if (token.type == Token_Invalid)
            return false;
        if (token.type == Token_String) {
            if (!has_pending_key) {
                pending_key = token;
                has_pending_key = true;
                continue;
            }
            bool in_app = location->app.found && location->app.close_start == 0 && depth == Apps_Path_Depth + 1;
            if (in_app && token_equals(text, pending_key, "LaunchOptions")) {
                location->launch_options_key = pending_key;
                location->launch_options_value = token;
                location->has_launch_options = true;
            }
            has_pending_key = false;
            continue;
        }
        if (token.type == Token_Open) {
            if (!has_pending_key || depth >= Vdf_Max_Depth)
                return false;
            names[depth] = pending_key;
            open_ends[depth] = token.end;
            depth++;
            has_pending_key = false;
            bool under_apps_path = depth >= Apps_Path_Depth;
            for (int i = 0; under_apps_path && i < Apps_Path_Depth; i++)
                under_apps_path = token_equals(text, names[i], apps_path[i]);
            if (under_apps_path && depth == Apps_Path_Depth && !location->apps.found) {
                location->apps.found = true;
                location->apps.open_end = token.end;
            }
            if (under_apps_path && depth == Apps_Path_Depth + 1 && !location->app.found
                && token_equals(text, names[depth - 1], app_id)) {
                location->app.found = true;
                location->app.open_end = token.end;
            }
            continue;
        }
        if (has_pending_key || depth == 0)
            return false;
        if (depth == Apps_Path_Depth + 1 && location->app.found && location->app.close_start == 0
            && open_ends[depth - 1] == location->app.open_end)
            location->app.close_start = token.start;
        if (depth == Apps_Path_Depth && location->apps.found && location->apps.close_start == 0
            && open_ends[depth - 1] == location->apps.open_end)
            location->apps.close_start = token.start;
        depth--;
    }
}

static size_t line_start(const char *text, size_t position) {
    while (position > 0 && text[position - 1] != '\n')
        position--;
    return position;
}

static size_t line_end(const char *text, size_t length, size_t position) {
    while (position < length && text[position] != '\n')
        position++;
    return position < length ? position + 1 : position;
}

static char *indentation_of(const char *text, size_t position) {
    size_t start = line_start(text, position);
    size_t end = start;
    while (text[end] == '\t' || text[end] == ' ')
        end++;
    char *indent = malloc(end - start + 1);
    memcpy(indent, text + start, end - start);
    indent[end - start] = '\0';
    return indent;
}

static char *escape_vdf_string(const char *value) {
    size_t length = strlen(value);
    char *escaped = malloc(length * 2 + 3);
    size_t out = 0;
    escaped[out++] = '"';
    for (size_t i = 0; i < length; i++) {
        if (value[i] == '"' || value[i] == '\\')
            escaped[out++] = '\\';
        escaped[out++] = value[i];
    }
    escaped[out++] = '"';
    escaped[out] = '\0';
    return escaped;
}

static char *splice(const char *text, size_t length, size_t start, size_t end, const char *replacement) {
    size_t replacement_length = strlen(replacement);
    char *result = malloc(length - (end - start) + replacement_length + 1);
    memcpy(result, text, start);
    memcpy(result + start, replacement, replacement_length);
    memcpy(result + start + replacement_length, text + end, length - end);
    result[length - (end - start) + replacement_length] = '\0';
    return result;
}

char *vdf_set_launch_options(const char *text, const char *app_id, const char *launch_options, VdfResult *result) {
    size_t length = strlen(text);
    AppLocation location;
    if (!locate_app(text, length, app_id, &location) || !location.apps.found) {
        *result = Vdf_Result_Unrecognized;
        return NULL;
    }

    if (launch_options == NULL) {
        if (!location.has_launch_options) {
            *result = Vdf_Result_Unchanged;
            return strdup(text);
        }
        size_t start = line_start(text, location.launch_options_key.start);
        size_t end = line_end(text, length, location.launch_options_value.end);
        *result = Vdf_Result_Changed;
        return splice(text, length, start, end, "");
    }

    char *value = escape_vdf_string(launch_options);
    char *updated = NULL;
    if (location.has_launch_options) {
        updated = splice(text, length, location.launch_options_value.start, location.launch_options_value.end, value);
    } else if (location.app.found) {
        char *indent = indentation_of(text, location.app.open_end - 1);
        size_t insert_at = line_end(text, length, location.app.open_end);
        size_t entry_size = strlen(indent) + strlen(value) + 32;
        char *entry = malloc(entry_size);
        snprintf(entry, entry_size, "%s\t\"LaunchOptions\"\t\t%s\n", indent, value);
        updated = splice(text, length, insert_at, insert_at, entry);
        free(entry);
        free(indent);
    } else {
        char *indent = indentation_of(text, location.apps.open_end - 1);
        size_t insert_at = line_end(text, length, location.apps.open_end);
        size_t entry_size = strlen(indent) * 3 + strlen(app_id) + strlen(value) + 64;
        char *entry = malloc(entry_size);
        snprintf(entry, entry_size, "%s\t\"%s\"\n%s\t{\n%s\t\t\"LaunchOptions\"\t\t%s\n%s\t}\n",
            indent, app_id, indent, indent, value, indent);
        updated = splice(text, length, insert_at, insert_at, entry);
        free(entry);
        free(indent);
    }
    free(value);
    *result = Vdf_Result_Changed;
    return updated;
}

bool vdf_get_launch_options(const char *text, const char *app_id, char *out_value, size_t out_value_size) {
    AppLocation location;
    if (!locate_app(text, strlen(text), app_id, &location) || !location.has_launch_options)
        return false;
    size_t out = 0;
    for (size_t i = location.launch_options_value.start + 1; i + 1 < location.launch_options_value.end && out + 1 < out_value_size; i++) {
        if (text[i] == '\\' && i + 2 < location.launch_options_value.end)
            i++;
        out_value[out++] = text[i];
    }
    out_value[out] = '\0';
    return true;
}
