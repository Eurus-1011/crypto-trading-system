#include "client/okx/okx_client.hpp"
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sstream>

namespace cts {

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string base64_encode(const unsigned char* data, int len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (int i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? table[n & 0x3F] : '=');
    }
    return out;
}

OkxClient::OkxClient(const std::string& api_key, const std::string& secret_key, const std::string& passphrase,
                     const std::string& base_url, bool simulated)
    : api_key_(api_key), secret_key_(secret_key), passphrase_(passphrase), base_url_(base_url),
      simulated_(simulated) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

OkxClient::~OkxClient() {
    curl_global_cleanup();
}

std::string OkxClient::iso_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    std::ostringstream oss;
    oss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

std::string OkxClient::sign(const std::string& timestamp, const std::string& method, const std::string& path,
                            const std::string& body) const {
    std::string prehash = timestamp + method + path + body;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), secret_key_.data(), static_cast<int>(secret_key_.size()),
         reinterpret_cast<const unsigned char*>(prehash.data()), prehash.size(), digest, &digest_len);
    return base64_encode(digest, static_cast<int>(digest_len));
}

OkxClient::Headers OkxClient::auth_headers(const std::string& method, const std::string& path,
                                           const std::string& body) const {
    std::string ts = iso_timestamp();
    Headers h;
    h["OK-ACCESS-KEY"] = api_key_;
    h["OK-ACCESS-SIGN"] = sign(ts, method, path, body);
    h["OK-ACCESS-TIMESTAMP"] = ts;
    h["OK-ACCESS-PASSPHRASE"] = passphrase_;
    h["Content-Type"] = "application/json";
    if (simulated_) h["x-simulated-trading"] = "1";
    return h;
}

std::string OkxClient::http_get(const std::string& url, const Headers& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    std::string response;
    struct curl_slist* hlist = nullptr;
    for (auto& [k, v] : headers) {
        hlist = curl_slist_append(hlist, (k + ": " + v).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_perform(curl);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    return response;
}

std::string OkxClient::http_post(const std::string& url, const std::string& body, const Headers& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    std::string response;
    struct curl_slist* hlist = nullptr;
    for (auto& [k, v] : headers) {
        hlist = curl_slist_append(hlist, (k + ": " + v).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_perform(curl);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    return response;
}

Json::Value OkxClient::parse_json(const std::string& raw) {
    Json::CharReaderBuilder builder;
    Json::Value val;
    std::string errs;
    std::istringstream ss(raw);
    Json::parseFromStream(builder, ss, &val, &errs);
    return val;
}

Json::Value OkxClient::get_ticker(const std::string& instrument) {
    std::string path = "/api/v5/market/ticker?instId=" + instrument;
    Headers h;
    if (simulated_) h["x-simulated-trading"] = "1";
    return parse_json(http_get(base_url_ + path, h));
}

Json::Value OkxClient::place_order(const std::string& instrument, const std::string& side,
                                   const std::string& ord_type, const std::string& size,
                                   const std::string& tgt_ccy) {
    std::string path = "/api/v5/trade/order";
    Json::Value body;
    body["instId"] = instrument;
    body["tdMode"] = "cash";
    body["side"] = side;
    body["ordType"] = ord_type;
    body["sz"] = size;
    if (ord_type == "market" && side == "buy") {
        body["tgtCcy"] = tgt_ccy;
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body_str = Json::writeString(wb, body);
    Headers h = auth_headers("POST", path, body_str);
    return parse_json(http_post(base_url_ + path, body_str, h));
}

} // namespace cts
