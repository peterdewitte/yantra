/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A small exporter plugin written against the public C ABI only
 * (plugin-api/include/yantra/plugin.h, PLG-02, PLG-03), for the export tests
 * (EXP-04): its format "rows_text" (offered in batch export) writes each
 * table it gets as text - the provenance, the field names with their
 * types and units, then one line per record with the values ("-" when
 * absent), tab-separated. Its second format "never_batch" is not offered
 * in batch export. The job is allocated and freed by the plugin.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yantra/plugin.h"

struct yantra_plugin_instance {
    const yantra_host_api* host;
};
static struct yantra_plugin_instance the_instance;

struct yantra_export_job {
    FILE* file;
    char* path;
    uint64_t rows;
};

static const yantra_export_format format_rows = {sizeof(yantra_export_format), YANTRA_EXPORT_BATCH, "rows_text",
                                                 "Rows as text", "txt", "Every table as tab-separated text."};
static const yantra_export_format format_never = {sizeof(yantra_export_format), 0, "never_batch",
                                                  "Not in batch", "txt", "Not offered in batch export."};
static const yantra_export_format* const formats[] = {&format_rows, &format_never};

static yantra_status exp_begin(yantra_plugin_instance* instance, const yantra_export_request* r,
                               yantra_export_job** out_job)
{
    struct yantra_export_job* job;
    (void)instance;
    if (r == NULL || out_job == NULL || r->path == NULL || r->format_id == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (strcmp(r->format_id, "rows_text") != 0 && strcmp(r->format_id, "never_batch") != 0)
        return YANTRA_ERR_INVALID_ARGUMENT;
    job = (struct yantra_export_job*)calloc(1, sizeof *job);
    if (job == NULL)
        return YANTRA_ERR_FAILED;
    job->path = (char*)malloc(strlen(r->path) + 1);
    if (job->path == NULL) {
        free(job);
        return YANTRA_ERR_FAILED;
    }
    strcpy(job->path, r->path);
    /* Binary mode: LF line ends on Windows too (text mode writes CRLF). */
    job->file = fopen(r->path, "wb");
    if (job->file == NULL) {
        free(job->path);
        free(job);
        return YANTRA_ERR_FAILED;
    }
    fprintf(job->file, "format %s\noptions %s\n", r->format_id, r->options != NULL ? r->options : "");
    *out_job = job;
    return YANTRA_OK;
}

static const char* type_name(uint32_t t)
{
    switch (t) {
    case YANTRA_TYPE_BOOL: return "bool";
    case YANTRA_TYPE_INT: return "int";
    case YANTRA_TYPE_UINT: return "uint";
    case YANTRA_TYPE_DOUBLE: return "double";
    case YANTRA_TYPE_STRING: return "string";
    case YANTRA_TYPE_ENUM: return "enum";
    default: return "none";
    }
}

static yantra_status exp_write(yantra_export_job* job, const yantra_provenance* p, const yantra_schema* s,
                               const yantra_record* const* rows, uint64_t row_count)
{
    uint32_t i;
    uint64_t k;
    if (job == NULL || p == NULL || s == NULL || (row_count > 0 && rows == NULL))
        return YANTRA_ERR_INVALID_ARGUMENT;
    fprintf(job->file, "table %s %s %s %u\n", p->plugin_id, p->schema_id, s->id, (unsigned)s->version);
    for (i = 0; i < s->field_count; ++i)
        fprintf(job->file, "%s%s:%s:%s", i ? "\t" : "", s->fields[i]->name, type_name(s->fields[i]->type),
                s->fields[i]->unit);
    fputc('\n', job->file);
    for (k = 0; k < row_count; ++k) {
        const yantra_record* r = rows[k];
        if (r->schema_index != 0 || r->value_count > s->field_count)
            return YANTRA_ERR_INVALID_ARGUMENT;
        for (i = 0; i < r->value_count; ++i) {
            const yantra_value* v = &r->values[i];
            if (i)
                fputc('\t', job->file);
            if (v->type != YANTRA_TYPE_NONE && v->type != s->fields[i]->type)
                return YANTRA_ERR_INVALID_ARGUMENT;
            switch (v->type) {
            case YANTRA_TYPE_BOOL:
            case YANTRA_TYPE_INT:
            case YANTRA_TYPE_ENUM: fprintf(job->file, "%lld", (long long)v->u.i64); break;
            case YANTRA_TYPE_UINT: fprintf(job->file, "%llu", (unsigned long long)v->u.u64); break;
            case YANTRA_TYPE_DOUBLE: fprintf(job->file, "%.17g", v->u.f64); break;
            case YANTRA_TYPE_STRING: fputs(v->u.str, job->file); break;
            default: fputc('-', job->file); break;
            }
        }
        fprintf(job->file, "\t@%lld\n", (long long)r->time.utc_ns);
        ++job->rows;
    }
    return YANTRA_OK;
}

static void close_job(yantra_export_job* job)
{
    if (job->file != NULL)
        fclose(job->file);
    free(job->path);
    free(job);
}

static yantra_status exp_finish(yantra_export_job* job)
{
    if (job == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    fprintf(job->file, "rows %llu\n", (unsigned long long)job->rows);
    close_job(job);
    return YANTRA_OK;
}

static void exp_abort(yantra_export_job* job)
{
    if (job == NULL)
        return;
    if (job->file != NULL)
        fclose(job->file);
    job->file = NULL;
    remove(job->path);
    close_job(job);
}

static const yantra_exporter_api exporter = {sizeof(yantra_exporter_api), 2, formats, exp_begin,
                                             exp_write, exp_finish, exp_abort};

static yantra_status plugin_init(const yantra_host_api* host, yantra_plugin_instance** out)
{
    if (host == NULL || out == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    the_instance.host = host;
    *out = &the_instance;
    return YANTRA_OK;
}

static void plugin_shutdown(yantra_plugin_instance* instance)
{
    instance->host = NULL;
}

static const yantra_plugin_descriptor descriptor = {
    sizeof(yantra_plugin_descriptor),
    {YANTRA_PLUGIN_API_VERSION_MAJOR, YANTRA_PLUGIN_API_VERSION_MINOR},
    YANTRA_KIND_EXPORTER,
    YANTRA_CATEGORY_SYSTEM_INFO, /* at least one (PLG-08); an exporter produces no records */
    0,
    "org.example.rows_exporter",
    "Rows exporter (test)",
    "1.0.0",
    "The Yantra contributors",
    "GPL-3.0-or-later",
    "Writes exported rows as text, for the export tests.",
    NULL,
    NULL,
    plugin_init,
    plugin_shutdown,
    NULL,
    NULL,
    NULL,
    &exporter,
    NULL,
};

YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(const yantra_host_api* host)
{
    if (host == NULL || host->api_version.major != YANTRA_PLUGIN_API_VERSION_MAJOR)
        return NULL;
    return &descriptor;
}
