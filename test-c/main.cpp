// This example borrows heavily from https://gist.github.com/ac000/906707af37b42cdcc9d3cc4c2715da99#file-002-luw-echo-request-c-L1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include <algorithm>
#include <vector>
#include <duckdb.hpp>
#include <duckdb/common/http_util.hpp>
#include <duckdb/main/config.hpp>
#include <duckdb/httpfs/httpfs_client.hpp>
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include "spin3_http.h"

#define MAX_PATH 1024
#define MAX_READ_BYTES (8 * 1024 * 1024)
#define BUFFER_SIZE 500000

#define BUF_ADD(fmt, ...) \
    fprintf(out, fmt, ##__VA_ARGS__);

static const struct
{
    const char *method;
} http_method_map[] = {
    {"GET"},
    {"HEAD"},
    {"POST"},
    {"PUT"},
    {"DELETE"},
    {"CONNECT"},
    {"OPTIONS"},
    {"TRACE"},
    {"PATCH"},
    {"OTHER"}};

#define AUTH_AUTHORITY "test-oauth.spin.internal"
// Chained requests bypass the `/auth/...` route, so the oauth component sees the raw path.
#define AUTH_USER_PATH "/user"
#define UNAUTHORIZED_MESSAGE "401 Unauthorized: please sign in at /auth to view this page.\n"

static std::string sql_string(const char *value)
{
    std::string quoted = "'";
    for (const char *ptr = value; *ptr; ++ptr)
    {
        quoted += *ptr == '\'' ? "''" : std::string(1, *ptr);
    }
    return quoted + "'";
}

class SpinHTTPClient : public duckdb::HTTPClient
{
public:
    explicit SpinHTTPClient(const duckdb::string &base_url) : HTTPClient(base_url) {}

    void Initialize(duckdb::HTTPParams &) override {}

    duckdb::unique_ptr<duckdb::HTTPResponse> Get(duckdb::GetRequestInfo &info) override
    {
        return Request(info, WASI_HTTP_TYPES_METHOD_GET, info.response_handler, info.content_handler);
    }

    duckdb::unique_ptr<duckdb::HTTPResponse> Head(duckdb::HeadRequestInfo &info) override
    {
        return Request(info, WASI_HTTP_TYPES_METHOD_HEAD);
    }

    duckdb::unique_ptr<duckdb::HTTPResponse> Put(duckdb::PutRequestInfo &) override { return Unsupported(); }
    duckdb::unique_ptr<duckdb::HTTPResponse> Delete(duckdb::DeleteRequestInfo &) override { return Unsupported(); }
    duckdb::unique_ptr<duckdb::HTTPResponse> Post(duckdb::PostRequestInfo &) override { return Unsupported(); }

private:
    duckdb::unique_ptr<duckdb::HTTPResponse> Unsupported()
    {
        auto response = duckdb::make_uniq<duckdb::HTTPResponse>(duckdb::HTTPStatusCode::NotImplemented_501);
        response->request_error = "Spin DuckDB HTTP transport only supports GET and HEAD";
        return response;
    }

    duckdb::unique_ptr<duckdb::HTTPResponse> Request(
        duckdb::BaseRequest &info,
        uint8_t method_tag,
        const std::function<bool(const duckdb::HTTPResponse &)> &response_handler = {},
        const std::function<bool(duckdb::const_data_ptr_t, duckdb::idx_t)> &content_handler = {})
    {
        wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
        wasi_http_types_borrow_fields_t borrowed_fields = wasi_http_types_borrow_fields(fields);
        for (const auto &header : duckdb::BaseRequest::MergeHeaders(info.headers, info.params))
        {
            wasi_http_types_field_name_t name;
            wasi_http_types_field_value_t value;
            wasi_http_types_header_error_t header_error;
            spin3_http_string_set(&name, header.first.c_str());
            spin3_http_string_set(reinterpret_cast<spin3_http_string_t *>(&value), header.second.c_str());
            wasi_http_types_method_fields_append(borrowed_fields, &name, &value, &header_error);
        }

        wasi_http_types_own_outgoing_request_t request = wasi_http_types_constructor_outgoing_request(fields);
        wasi_http_types_borrow_outgoing_request_t borrowed_request = wasi_http_types_borrow_outgoing_request(request);
        wasi_http_types_method_t method = {.tag = method_tag};
        wasi_http_types_scheme_t scheme;
        duckdb::string path;
        duckdb::string protocol_and_authority;
        duckdb::HTTPUtil::DecomposeURL(info.url, path, protocol_and_authority);
        const bool https = protocol_and_authority.rfind("https://", 0) == 0;
        scheme.tag = https ? WASI_HTTP_TYPES_SCHEME_HTTPS : WASI_HTTP_TYPES_SCHEME_HTTP;
        duckdb::string authority = protocol_and_authority.substr(https ? 8 : 7);
        spin3_http_string_t wit_path;
        spin3_http_string_t wit_authority;
        spin3_http_string_set(&wit_path, path.c_str());
        spin3_http_string_set(&wit_authority, authority.c_str());
        wasi_http_types_method_outgoing_request_set_method(borrowed_request, &method);
        wasi_http_types_method_outgoing_request_set_scheme(borrowed_request, &scheme);
        wasi_http_types_method_outgoing_request_set_authority(borrowed_request, &wit_authority);
        wasi_http_types_method_outgoing_request_set_path_with_query(borrowed_request, &wit_path);

        wasi_http_types_own_future_incoming_response_t future;
        wasi_http_outgoing_handler_error_code_t handler_error;
        if (!wasi_http_outgoing_handler_handle(request, NULL, &future, &handler_error))
        {
            auto response = duckdb::make_uniq<duckdb::HTTPResponse>(duckdb::HTTPStatusCode::INVALID);
            response->request_error = "Spin wasi:http rejected the outgoing request";
            wasi_http_outgoing_handler_error_code_free(&handler_error);
            return response;
        }

        auto borrowed_future = wasi_http_types_borrow_future_incoming_response(future);
        auto pollable = wasi_http_types_method_future_incoming_response_subscribe(borrowed_future);
        wasi_io_poll_method_pollable_block(wasi_io_poll_borrow_pollable(pollable));
        wasi_io_poll_pollable_drop_own(pollable);

        wasi_http_types_result_result_own_incoming_response_error_code_void_t result;
        if (!wasi_http_types_method_future_incoming_response_get(borrowed_future, &result) ||
            result.is_err || result.val.ok.is_err)
        {
            auto response = duckdb::make_uniq<duckdb::HTTPResponse>(duckdb::HTTPStatusCode::INVALID);
            response->request_error = "Spin wasi:http request failed";
            wasi_http_types_result_result_own_incoming_response_error_code_void_free(&result);
            wasi_http_types_future_incoming_response_drop_own(future);
            return response;
        }

        wasi_http_types_own_incoming_response_t incoming = result.val.ok.val.ok;
        auto borrowed_incoming = wasi_http_types_borrow_incoming_response(incoming);
        auto status = wasi_http_types_method_incoming_response_status(borrowed_incoming);
        auto response = duckdb::make_uniq<duckdb::HTTPResponse>(static_cast<duckdb::HTTPStatusCode>(status));
        response->url = info.url;
        response->reason = duckdb::HTTPUtil::GetStatusMessage(static_cast<duckdb::HTTPStatusCode>(status));

        auto response_headers = wasi_http_types_method_incoming_response_headers(borrowed_incoming);
        wasi_http_types_list_tuple2_field_name_field_value_t entries;
        wasi_http_types_method_fields_entries(wasi_http_types_borrow_fields(response_headers), &entries);
        for (size_t index = 0; index < entries.len; ++index)
        {
            response->headers.Insert(
                duckdb::string(reinterpret_cast<const char *>(entries.ptr[index].f0.ptr), entries.ptr[index].f0.len),
                duckdb::string(reinterpret_cast<const char *>(entries.ptr[index].f1.ptr), entries.ptr[index].f1.len));
        }
        wasi_http_types_list_tuple2_field_name_field_value_free(&entries);
        wasi_http_types_fields_drop_own(response_headers);

        bool read_body = !response_handler || response_handler(*response);

        wasi_http_types_own_incoming_body_t body;
        if (read_body && method_tag != WASI_HTTP_TYPES_METHOD_HEAD &&
            wasi_http_types_method_incoming_response_consume(borrowed_incoming, &body))
        {
            wasi_io_streams_own_input_stream_t stream;
            if (wasi_http_types_method_incoming_body_stream(wasi_http_types_borrow_incoming_body(body), &stream))
            {
                auto borrowed_stream = wasi_io_streams_borrow_input_stream(stream);
                while (true)
                {
                    spin3_http_list_u8_t chunk = {0};
                    wasi_io_streams_stream_error_t stream_error;
                    if (!wasi_io_streams_method_input_stream_blocking_read(borrowed_stream, 64 * 1024, &chunk, &stream_error))
                    {
                        if (stream_error.tag != WASI_IO_STREAMS_STREAM_ERROR_CLOSED)
                        {
                            response->request_error = "Spin wasi:http response body read failed";
                        }
                        wasi_io_streams_stream_error_free(&stream_error);
                        break;
                    }
                    if (content_handler)
                    {
                        content_handler(chunk.ptr, chunk.len);
                    }
                    else
                    {
                        response->body.append(reinterpret_cast<const char *>(chunk.ptr), chunk.len);
                    }
                    spin3_http_list_u8_free(&chunk);
                }
                wasi_io_streams_input_stream_drop_own(stream);
            }
            wasi_http_types_incoming_body_drop_own(body);
        }

        wasi_http_types_incoming_response_drop_own(incoming);
        wasi_http_types_result_result_own_incoming_response_error_code_void_free(&result);
        wasi_http_types_future_incoming_response_drop_own(future);
        return response;
    }
};

// Must derive from HTTPFSUtil so InitializeParameters yields the HTTPFSParams that httpfs casts to.
class SpinHTTPUtil : public duckdb::HTTPFSUtil
{
public:
    duckdb::string GetName() const override { return "WasmHTTPUtils"; }

    duckdb::unique_ptr<duckdb::HTTPClient> InitializeClient(
        duckdb::HTTPParams &, const duckdb::string &base_url) override
    {
        return duckdb::make_uniq<SpinHTTPClient>(base_url);
    }
};

// Plain pointer (no static ctor) so a Wizer snapshot keeps it across instantiation.
static duckdb::DuckDB *warm_database;

static duckdb::DuckDB &get_database()
{
    if (!warm_database)
    {
        warm_database = new duckdb::DuckDB(nullptr);
        duckdb::DBConfig::GetConfig(*warm_database->instance).SetHTTPUtil(duckdb::make_shared_ptr<SpinHTTPUtil>());
    }
    return *warm_database;
}

extern "C" int __getentropy(void *buffer, size_t length);

static bool wizer_snapshotting;

static void fill_snapshot_bytes(void *buffer, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        static_cast<uint8_t *>(buffer)[i] = static_cast<uint8_t>(i * 131 + 17);
    }
}

// Overrides wasi-libc's weak getentropy: host imports trap under Wizer, so fill deterministically there.
extern "C" int getentropy(void *buffer, size_t length)
{
    if (wizer_snapshotting)
    {
        fill_snapshot_bytes(buffer, length);
        return 0;
    }
    return __getentropy(buffer, length);
}

extern "C" int __clock_gettime(clockid_t clock, struct timespec *ts);

// Overrides wasi-libc's weak clock_gettime for the same reason.
extern "C" int clock_gettime(clockid_t clock, struct timespec *ts)
{
    if (wizer_snapshotting)
    {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
        return 0;
    }
    return __clock_gettime(clock, ts);
}

// Stands in for OpenSSL's DRBG while snapshotting so no seeded CSPRNG state is baked into the binary.
static int snapshot_rand_bytes(unsigned char *buffer, int length)
{
    fill_snapshot_bytes(buffer, static_cast<size_t>(length));
    return 1;
}

static int snapshot_rand_status(void) { return 1; }

static const RAND_METHOD snapshot_rand_method = {
    nullptr, snapshot_rand_bytes, nullptr, nullptr, snapshot_rand_bytes, snapshot_rand_status};

extern "C" char **__wasilibc_environ;

// Wizer drops the start function that sets the TLS base, so the snapshot value is restored on resume.
extern "C" __attribute__((import_module("env"), import_name("__wasm_get_tls_base"))) void *wasm_get_tls_base(void);
extern "C" __attribute__((import_module("env"), import_name("__wasm_set_tls_base"))) void wasm_set_tls_base(void *base);
static void *snapshot_tls_base;

// Run by Wizer at build time (after `_initialize`); the snapshot then replaces `_initialize` with `wizer-resume`.
extern "C" __attribute__((export_name("wizer-initialize"))) void wizer_initialize(void)
{
    // An empty environ stops getenv from calling the host; (char **)-1 restores wasi-libc's lazy load.
    static char *empty_environ[] = {nullptr};
    __wasilibc_environ = empty_environ;
    wizer_snapshotting = true;
    // There is no openssl.cnf in the guest, and reading it would hit the host filesystem.
    OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
    RAND_set_rand_method(&snapshot_rand_method);
    get_database();
    RAND_set_rand_method(nullptr);
    wizer_snapshotting = false;
    __wasilibc_environ = reinterpret_cast<char **>(-1);
    snapshot_tls_base = wasm_get_tls_base();
}

extern "C" __attribute__((export_name("wizer-resume"))) void wizer_resume(void)
{
    wasm_set_tls_base(snapshot_tls_base);
}

static bool get_spin_variable(const char *name, std::string &value)
{
    spin3_http_string_t variable_name;
    spin3_http_string_t variable_value;
    fermyon_spin_2_0_0_variables_error_t error;
    spin3_http_string_set(&variable_name, name);
    if (!fermyon_spin_2_0_0_variables_get(&variable_name, &variable_value, &error))
    {
        fermyon_spin_2_0_0_variables_error_free(&error);
        return false;
    }
    value.assign(reinterpret_cast<const char *>(variable_value.ptr), variable_value.len);
    spin3_http_string_free(&variable_value);
    return true;
}

static void append_query_result(FILE *out, duckdb::MaterializedQueryResult &result)
{
    BUF_ADD("Rows: %llu\n", (unsigned long long)result.RowCount());
    for (duckdb::idx_t col = 0; col < result.ColumnCount(); ++col)
    {
        BUF_ADD("%s%s", col ? " | " : "", result.ColumnName(col).c_str());
    }
    BUF_ADD("\n");
    for (duckdb::idx_t row = 0; row < result.RowCount(); ++row)
    {
        for (duckdb::idx_t col = 0; col < result.ColumnCount(); ++col)
        {
            BUF_ADD("%s%s", col ? " | " : "", result.GetValue(col, row).ToString().c_str());
        }
        BUF_ADD("\n");
    }
    BUF_ADD("\n");
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static std::string url_decode(const std::string &encoded)
{
    std::string decoded;
    for (size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '+')
        {
            decoded += ' ';
        }
        else if (encoded[i] == '%' && i + 2 < encoded.size() &&
                 hex_value(encoded[i + 1]) >= 0 && hex_value(encoded[i + 2]) >= 0)
        {
            decoded += static_cast<char>(hex_value(encoded[i + 1]) * 16 + hex_value(encoded[i + 2]));
            i += 2;
        }
        else
        {
            decoded += encoded[i];
        }
    }
    return decoded;
}

// Finds `name` in an application/x-www-form-urlencoded string (query string or POST body).
static bool get_form_param(const std::string &form, const char *name, std::string &value)
{
    size_t start = 0;
    while (start <= form.size())
    {
        size_t end = form.find('&', start);
        if (end == std::string::npos)
            end = form.size();
        std::string pair = form.substr(start, end - start);
        size_t eq = pair.find('=');
        if (url_decode(pair.substr(0, eq)) == name)
        {
            value = eq == std::string::npos ? "" : url_decode(pair.substr(eq + 1));
            return true;
        }
        start = end + 1;
    }
    return false;
}

static void append_s3_metadata(FILE *out, const std::string &user_sql)
{
    std::string endpoint;
    std::string bucket_host;
    std::string bucket;
    std::string access_key;
    std::string secret_key;
    if (!get_spin_variable("s3_endpoint", endpoint) ||
        !get_spin_variable("s3_bucket", bucket_host) ||
        !get_spin_variable("s3_label", bucket) ||
        !get_spin_variable("s3_access_key", access_key) ||
        !get_spin_variable("s3_secret_key", secret_key) ||
        endpoint.empty() || bucket_host.empty() || bucket.empty() ||
        access_key.empty() || secret_key.empty())
    {
        BUF_ADD("[S3 Metadata]\nSpin S3 variables are incomplete.\n\n");
        return;
    }

    duckdb::Connection connection(get_database());
    std::string create_secret =
        "CREATE SECRET spin_s3 (TYPE S3, KEY_ID " + sql_string(access_key.c_str()) +
        ", SECRET " + sql_string(secret_key.c_str()) +
        ", ENDPOINT " + sql_string(endpoint.c_str()) +
        ", URL_STYLE 'vhost', USE_SSL true)";
    auto secret_result = connection.Query(create_secret);
    if (secret_result->HasError())
    {
        BUF_ADD("[S3 Metadata]\nDuckDB S3 setup failed: %s\n\n", secret_result->GetError().c_str());
        return;
    }
    connection.Query("SET force_download_threshold = 0");

    if (!user_sql.empty())
    {
        BUF_ADD("[SQL]\n%s\n\n[Result]\n", user_sql.c_str());
        auto user_result = connection.Query(user_sql);
        if (user_result->HasError())
        {
            BUF_ADD("DuckDB query failed: %s\n\n", user_result->GetError().c_str());
            return;
        }
        append_query_result(out, *user_result);
        return;
    }

    std::string source = "s3://" + bucket + "/*";
    auto metadata = connection.Query(
        "SELECT file FROM glob(" + sql_string(source.c_str()) + ") ORDER BY file LIMIT 20");
    BUF_ADD("[S3 Metadata]\nBucket: %s\n", bucket_host.c_str());
    if (metadata->HasError())
    {
        BUF_ADD("DuckDB metadata read failed: %s\n\n", metadata->GetError().c_str());
        return;
    }

    BUF_ADD("Objects: %llu\n", (unsigned long long)metadata->RowCount());
    for (duckdb::idx_t row = 0; row < metadata->RowCount(); ++row)
    {
        BUF_ADD("%s\n", metadata->GetValue(0, row).ToString().c_str());
    }
    BUF_ADD("\n");

    if (metadata->RowCount() == 0)
    {
        return;
    }

    std::string object_path = metadata->GetValue(0, 0).ToString();
    auto schema = connection.Query(
        "DESCRIBE SELECT * FROM read_parquet(" + sql_string(object_path.c_str()) + ")");
    BUF_ADD("[File Schema]\nFile: %s\nFormat: Parquet\n", object_path.c_str());
    if (schema->HasError())
    {
        BUF_ADD("DuckDB schema read failed: %s\n\n", schema->GetError().c_str());
        return;
    }

    BUF_ADD("Columns: %llu\n", (unsigned long long)schema->RowCount());
    for (duckdb::idx_t row = 0; row < schema->RowCount(); ++row)
    {
        BUF_ADD("%s | %s\n",
                schema->GetValue(0, row).ToString().c_str(),
                schema->GetValue(1, row).ToString().c_str());
    }
    BUF_ADD("\n");

    // No SQL ORDER BY: DuckDB's VARCHAR sort-key decode faults on wasm32.
    auto counts = connection.Query(
        "SELECT category, COUNT(*) AS records FROM read_parquet(" + sql_string(object_path.c_str()) +
        ") GROUP BY category");
    BUF_ADD("[Records per Category]\n");
    if (counts->HasError())
    {
        BUF_ADD("DuckDB aggregate failed: %s\n\n", counts->GetError().c_str());
        return;
    }
    std::vector<std::pair<std::string, std::string>> rows;
    for (duckdb::idx_t row = 0; row < counts->RowCount(); ++row)
    {
        rows.emplace_back(counts->GetValue(0, row).ToString(), counts->GetValue(1, row).ToString());
    }
    std::sort(rows.begin(), rows.end());
    for (const auto &row : rows)
    {
        BUF_ADD("%s | %s\n", row.first.c_str(), row.second.c_str());
    }
    BUF_ADD("\n");
}

// Forwards the caller's cookies to /auth/user via Spin service chaining; 200 means signed in.
static bool is_logged_in(wasi_http_types_borrow_fields_t req_hdrs)
{
    wasi_http_types_field_name_t name;
    wasi_http_types_list_field_value_t cookies;
    wasi_http_types_header_error_t hdr_err;
    spin3_http_string_set(&name, "cookie");
    wasi_http_types_method_fields_get(req_hdrs, &name, &cookies);

    wasi_http_types_own_fields_t hdrs = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t b_hdrs = wasi_http_types_borrow_fields(hdrs);
    for (size_t i = 0; i < cookies.len; i++)
    {
        wasi_http_types_method_fields_append(b_hdrs, &name, &cookies.ptr[i], &hdr_err);
    }
    wasi_http_types_list_field_value_free(&cookies);

    wasi_http_types_own_outgoing_request_t req = wasi_http_types_constructor_outgoing_request(hdrs);
    wasi_http_types_borrow_outgoing_request_t b_req = wasi_http_types_borrow_outgoing_request(req);
    wasi_http_types_scheme_t scheme;
    scheme.tag = WASI_HTTP_TYPES_SCHEME_HTTP;
    spin3_http_string_t authority;
    spin3_http_string_t path;
    spin3_http_string_set(&authority, AUTH_AUTHORITY);
    spin3_http_string_set(&path, AUTH_USER_PATH);
    wasi_http_types_method_outgoing_request_set_scheme(b_req, &scheme);
    wasi_http_types_method_outgoing_request_set_authority(b_req, &authority);
    wasi_http_types_method_outgoing_request_set_path_with_query(b_req, &path);

    wasi_http_types_own_future_incoming_response_t future;
    wasi_http_outgoing_handler_error_code_t err;
    if (!wasi_http_outgoing_handler_handle(req, NULL, &future, &err))
    {
        wasi_http_outgoing_handler_error_code_free(&err);
        return false;
    }

    wasi_http_types_borrow_future_incoming_response_t b_future = wasi_http_types_borrow_future_incoming_response(future);
    wasi_io_poll_own_pollable_t pollable = wasi_http_types_method_future_incoming_response_subscribe(b_future);
    wasi_io_poll_method_pollable_block(wasi_io_poll_borrow_pollable(pollable));
    wasi_io_poll_pollable_drop_own(pollable);

    bool logged_in = false;
    wasi_http_types_result_result_own_incoming_response_error_code_void_t result;
    if (wasi_http_types_method_future_incoming_response_get(b_future, &result))
    {
        if (!result.is_err && !result.val.ok.is_err)
        {
            wasi_http_types_own_incoming_response_t resp = result.val.ok.val.ok;
            logged_in = wasi_http_types_method_incoming_response_status(
                            wasi_http_types_borrow_incoming_response(resp)) == 200;
            wasi_http_types_incoming_response_drop_own(resp);
        }
        wasi_http_types_result_result_own_incoming_response_error_code_void_free(&result);
    }
    wasi_http_types_future_incoming_response_drop_own(future);
    return logged_in;
}

static void send_unauthorized(exports_wasi_http_incoming_handler_own_response_outparam_t response_out)
{
    wasi_http_types_field_key_t key;
    wasi_http_types_field_value_t value;
    wasi_http_types_header_error_t hdr_err;
    char clen[32];
    wasi_http_types_own_fields_t fields = wasi_http_types_constructor_fields();
    wasi_http_types_borrow_fields_t b_fields = wasi_http_types_borrow_fields(fields);

    spin3_http_string_set((spin3_http_string_t *)&key, "Content-Type");
    spin3_http_string_set((spin3_http_string_t *)&value, "text/plain; charset=utf-8");
    wasi_http_types_method_fields_append(b_fields, &key, &value, &hdr_err);

    spin3_http_string_set((spin3_http_string_t *)&key, "Content-Length");
    sprintf(clen, "%zu", strlen(UNAUTHORIZED_MESSAGE));
    spin3_http_string_set((spin3_http_string_t *)&value, clen);
    wasi_http_types_method_fields_append(b_fields, &key, &value, &hdr_err);

    wasi_http_types_own_outgoing_response_t resp = wasi_http_types_constructor_outgoing_response(fields);
    wasi_http_types_borrow_outgoing_response_t b_resp = wasi_http_types_borrow_outgoing_response(resp);
    wasi_http_types_method_outgoing_response_set_status_code(b_resp, 401);

    wasi_http_types_own_outgoing_body_t body;
    wasi_http_types_method_outgoing_response_body(b_resp, &body);

    wasi_http_types_result_own_outgoing_response_error_code_t result = {
        .is_err = false,
        .val = {
            .ok = resp}};
    wasi_http_types_static_response_outparam_set(response_out, &result);

    wasi_io_streams_own_output_stream_t out_stream;
    wasi_io_streams_stream_error_t stream_err;
    spin3_http_list_u8_t stream_data;
    wasi_http_types_method_outgoing_body_write(wasi_http_types_borrow_outgoing_body(body), &out_stream);
    stream_data.ptr = (uint8_t *)UNAUTHORIZED_MESSAGE;
    stream_data.len = strlen(UNAUTHORIZED_MESSAGE);
    wasi_io_streams_method_output_stream_blocking_write_and_flush(
        wasi_io_streams_borrow_output_stream(out_stream), &stream_data, &stream_err);
    wasi_io_streams_output_stream_drop_own(out_stream);

    wasi_http_types_error_code_t body_err;
    if (!wasi_http_types_static_outgoing_body_finish(body, NULL, &body_err))
    {
        wasi_http_types_error_code_free(&body_err);
    }
}

void exports_wasi_http_incoming_handler_handle(
    exports_wasi_http_incoming_handler_own_incoming_request_t request,
    exports_wasi_http_incoming_handler_own_response_outparam_t response_out)
{
    FILE *out;
    size_t size;
    char *out_ptr;
    char clen[32];
    wasi_http_types_borrow_incoming_request_t b_req;
    wasi_http_types_own_headers_t hdrs;
    wasi_http_types_borrow_fields_t b_hdrs;
    wasi_http_types_own_incoming_body_t r_body;
    wasi_http_types_borrow_incoming_body_t b_r_body;
    wasi_http_types_own_outgoing_response_t resp;
    wasi_http_types_borrow_outgoing_response_t b_resp;
    wasi_http_types_own_fields_t fields;
    wasi_http_types_borrow_fields_t b_fields;
    wasi_http_types_own_outgoing_body_t body;
    wasi_http_types_borrow_outgoing_body_t b_body;
    wasi_io_streams_own_output_stream_t out_stream;
    wasi_io_streams_borrow_output_stream_t b_out_stream;
    spin3_http_list_u8_t stream_data;
    wasi_io_streams_stream_error_t stream_err;
    wasi_http_types_header_error_t hdr_err;
    wasi_http_types_field_key_t key;
    wasi_http_types_field_value_t value;
    wasi_http_types_method_t method;
    wasi_http_types_list_tuple2_field_name_field_value_t fvk;
    wasi_http_types_own_input_stream_t in_stream;
    wasi_io_streams_borrow_input_stream_t b_in_stream;
    wasi_io_streams_stream_error_t in_stream_err;
    spin3_http_string_t prstr;
    bool ok;

    b_req = wasi_http_types_borrow_incoming_request(request);

    hdrs = wasi_http_types_method_incoming_request_headers(b_req);
    b_hdrs = wasi_http_types_borrow_fields(hdrs);
    // if (!is_logged_in(b_hdrs))
    // {
    //     send_unauthorized(response_out);
    //     return;
    // }

    wasi_http_types_method_incoming_request_path_with_query(b_req, &prstr);
    std::string path_with_query(reinterpret_cast<const char *>(prstr.ptr), prstr.len);
    spin3_http_string_free(&prstr);
    size_t query_start = path_with_query.find('?');
    std::string query = query_start == std::string::npos ? "" : path_with_query.substr(query_start + 1);
    wasi_http_types_method_incoming_request_method(b_req, &method);

    bool form_body = false;
    wasi_http_types_method_fields_entries(b_hdrs, &fvk);
    for (size_t i = 0; i < fvk.len; i++)
    {
        if (fvk.ptr[i].f0.len == 12 && strncasecmp((const char *)fvk.ptr[i].f0.ptr, "Content-Type", 12) == 0)
        {
            std::string content_type((const char *)fvk.ptr[i].f1.ptr, fvk.ptr[i].f1.len);
            form_body = content_type.rfind("application/x-www-form-urlencoded", 0) == 0;
        }
    }

    std::string request_body;
    std::string body_error;
    if (wasi_http_types_method_incoming_request_consume(b_req, &r_body))
    {
        b_r_body = wasi_http_types_borrow_incoming_body(r_body);
        if (wasi_http_types_method_incoming_body_stream(b_r_body, &in_stream))
        {
            b_in_stream = wasi_io_streams_borrow_input_stream(in_stream);
            while (request_body.size() < MAX_READ_BYTES)
            {
                spin3_http_list_u8_t chunk = {0};
                if (!wasi_io_streams_method_input_stream_blocking_read(
                        b_in_stream, MAX_READ_BYTES - request_body.size(), &chunk, &in_stream_err))
                {
                    if (in_stream_err.tag != WASI_IO_STREAMS_STREAM_ERROR_CLOSED)
                    {
                        body_error = "Error reading request body";
                    }
                    wasi_io_streams_stream_error_free(&in_stream_err);
                    break;
                }
                request_body.append(reinterpret_cast<const char *>(chunk.ptr), chunk.len);
                spin3_http_list_u8_free(&chunk);
            }
            wasi_io_streams_input_stream_drop_own(in_stream);
        }
        wasi_http_types_incoming_body_drop_own(r_body);
    }

    // SQL comes from the query string (GET) or a form-encoded body (POST); the body wins if both are set.
    std::string user_sql;
    get_form_param(query, "SQL", user_sql);
    if (method.tag == WASI_HTTP_TYPES_METHOD_POST && form_body)
    {
        get_form_param(request_body, "SQL", user_sql);
    }

    out = open_memstream(&out_ptr, &size);

    BUF_ADD("*** Spin with C++ http req/resp ***\n\n");
    BUF_ADD("DuckDB version: %s\n\n", duckdb::DuckDB::LibraryVersion());
    append_s3_metadata(out, user_sql);

    BUF_ADD("[Request Info]\n");
    BUF_ADD("REQUEST_PATH = %s\n", path_with_query.c_str());
    BUF_ADD("METHOD       = %s\n", http_method_map[method.tag].method);
    BUF_ADD("QUERY        = %s\n", query.c_str());

    BUF_ADD("\n[Request Headers]\n");
    for (size_t i = 0; i < fvk.len; i++)
    {
        BUF_ADD("%.*s = %.*s\n",
                (int)fvk.ptr[i].f0.len, fvk.ptr[i].f0.ptr,
                (int)fvk.ptr[i].f1.len, fvk.ptr[i].f1.ptr);
    }
    wasi_http_types_list_tuple2_field_name_field_value_free(&fvk);

    if (method.tag == WASI_HTTP_TYPES_METHOD_POST || method.tag == WASI_HTTP_TYPES_METHOD_PUT)
    {
        BUF_ADD("\n[%s data]\n", http_method_map[method.tag].method);
        BUF_ADD("%s\n", request_body.c_str());
    }
    if (!body_error.empty())
    {
        BUF_ADD("\n%s\n", body_error.c_str());
    }

    fclose(out);

    fields = wasi_http_types_constructor_fields();
    b_fields = wasi_http_types_borrow_fields(fields);

    spin3_http_string_set((spin3_http_string_t *)&key, "Content-Length");
    sprintf(clen, "%zu", size);
    spin3_http_string_set((spin3_http_string_t *)&value, clen);

    wasi_http_types_method_fields_append(b_fields, &key, &value, &hdr_err);

    resp = wasi_http_types_constructor_outgoing_response(fields);

    b_resp = wasi_http_types_borrow_outgoing_response(resp);
    wasi_http_types_method_outgoing_response_body(b_resp, &body);
    b_body = wasi_http_types_borrow_outgoing_body(body);

    wasi_http_types_method_outgoing_body_write(b_body, &out_stream);
    b_out_stream = wasi_io_streams_borrow_output_stream(out_stream);

    stream_data.len = size;
    stream_data.ptr = (uint8_t *)out_ptr;
    ok = wasi_io_streams_method_output_stream_blocking_write_and_flush(b_out_stream, &stream_data, &stream_err);

    free(out_ptr);

    wasi_http_types_result_own_outgoing_response_error_code_t result = {
        .is_err = false,
        .val = {
            .ok = resp}};
    wasi_http_types_static_response_outparam_set(response_out, &result);
}
