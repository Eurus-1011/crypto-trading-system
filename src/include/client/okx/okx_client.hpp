#pragma once

#include <json/json.h>
#include <map>
#include <string>

namespace cts {

class OkxClient {
public:
    OkxClient(const std::string& api_key, const std::string& secret_key, const std::string& passphrase,
              const std::string& base_url, bool simulated);
    ~OkxClient();

    OkxClient(const OkxClient&) = delete;
    OkxClient& operator=(const OkxClient&) = delete;

    Json::Value get_ticker(const std::string& instrument);

    Json::Value place_order(const std::string& instrument, const std::string& side, const std::string& ord_type,
                            const std::string& size, const std::string& tgt_ccy = "quote_ccy");

private:
    using Headers = std::map<std::string, std::string>;

    std::string sign(const std::string& timestamp, const std::string& method, const std::string& path,
                     const std::string& body) const;
    Headers auth_headers(const std::string& method, const std::string& path, const std::string& body = "") const;
    std::string iso_timestamp() const;

    static std::string http_get(const std::string& url, const Headers& headers);
    static std::string http_post(const std::string& url, const std::string& body, const Headers& headers);
    static Json::Value parse_json(const std::string& raw);

    std::string api_key_;
    std::string secret_key_;
    std::string passphrase_;
    std::string base_url_;
    bool simulated_;
};

} // namespace cts
