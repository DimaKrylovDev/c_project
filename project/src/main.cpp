#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr int kBacklogSize = 32;
    constexpr int kBufferSize = 8192;

    std::string toLower(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return result;
    }

    std::string trim(std::string_view value)
    {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string_view::npos)
        {
            return {};
        }
        const auto end = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(begin, end - begin + 1));
    }

    std::string urlDecode(const std::string &value)
    {
        std::string result;
        result.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '%' && i + 2 < value.size())
            {
                std::string hex = value.substr(i + 1, 2);
                char ch = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                result.push_back(ch);
                i += 2;
            }
            else if (value[i] == '+')
            {
                result.push_back(' ');
            }
            else
            {
                result.push_back(value[i]);
            }
        }
        return result;
    }

    std::unordered_map<std::string, std::string> parseParams(const std::string &data)
    {
        std::unordered_map<std::string, std::string> result;
        size_t start = 0;
        while (start < data.size())
        {
            const auto amp = data.find('&', start);
            const auto token = data.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
            const auto eq = token.find('=');
            if (eq != std::string::npos)
            {
                std::string key = urlDecode(token.substr(0, eq));
                std::string value = urlDecode(token.substr(eq + 1));
                result[std::move(key)] = std::move(value);
            }
            else if (!token.empty())
            {
                result[urlDecode(token)] = "";
            }
            if (amp == std::string::npos)
            {
                break;
            }
            start = amp + 1;
        }
        return result;
    }

    std::string jsonEscape(const std::string &value)
    {
        std::ostringstream oss;
        for (char ch : value)
        {
            switch (ch)
            {
            case '\"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    oss << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch))
                        << std::dec << std::setw(0);
                }
                else
                {
                    oss << ch;
                }
            }
        }
        return oss.str();
    }
}

struct HttpRequest
{
    std::string method;
    std::string rawTarget;
    std::string path;
    std::unordered_map<std::string, std::string> headers; // lower-case keys
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> form;
    std::string body;

    [[nodiscard]] std::string getHeader(const std::string &key) const
    {
        if (auto it = headers.find(key); it != headers.end())
        {
            return it->second;
        }
        return {};
    }

    [[nodiscard]] std::string getParam(const std::string &key) const
    {
        if (auto it = form.find(key); it != form.end())
        {
            return it->second;
        }
        if (auto it = query.find(key); it != query.end())
        {
            return it->second;
        }
        return {};
    }
};

struct HttpResponse
{
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    void setHeader(std::string key, std::string value)
    {
        headers.emplace_back(std::move(key), std::move(value));
    }
};

struct User
{
    int id = 0;
    std::string name;
    std::string email;
    std::string passwordHash;
};

struct Advertisement
{
    int id = 0;
    int ownerId = 0;
    std::string title;
    std::string description;
    double price = 0.0;
    std::time_t createdAt = 0;
};

// Структура для хранения откликов на объявления
// Ключ - ID объявления, значение - множество ID пользователей, откликнувшихся на это объявление
struct Response
{
    int userId = 0;
    int adId = 0;
    std::time_t respondedAt = 0;
};

class BulletinBoardApp
{
public:
    BulletinBoardApp();
    void run(uint16_t port);

private:
    bool parseRequest(int clientSock, HttpRequest &request) const;
    void routeRequest(const HttpRequest &request, HttpResponse &response);
    bool handleApi(const HttpRequest &request, HttpResponse &response);
    bool serveStatic(const std::string &path, HttpResponse &response) const;
    void sendResponse(int clientSock, const HttpResponse &response) const;
    std::optional<int> authenticate(const HttpRequest &request) const;

    // API handlers
    void handleRegister(const HttpRequest &request, HttpResponse &response);
    void handleLogin(const HttpRequest &request, HttpResponse &response);
    void handleLogout(const HttpRequest &request, HttpResponse &response);
    void handleSession(const HttpRequest &request, HttpResponse &response);
    void handleAdsList(const HttpRequest &request, HttpResponse &response);
    void handleCreateAd(const HttpRequest &request, HttpResponse &response);
    void handleDeleteAd(const HttpRequest &request, HttpResponse &response, int advertId);
    void handleRespondToAd(const HttpRequest &request, HttpResponse &response, int advertId);
    void handleMyResponses(const HttpRequest &request, HttpResponse &response);
    void handleAdResponders(const HttpRequest &request, HttpResponse &response, int advertId);

    // Helpers
    std::string readFileSafely(const std::filesystem::path &path) const;
    std::string guessMimeType(const std::filesystem::path &path) const;
    std::string buildAdsJson(int currentUserId) const;
    std::string userToJson(const User &user) const;
    std::string hashPassword(const std::string &password) const;
    std::string generateToken() const;

    // Data
    mutable std::mutex dataMutex_;
    std::vector<User> users_;
    std::vector<Advertisement> adverts_;
    std::unordered_map<std::string, int> emailToUserId_;
    std::unordered_map<std::string, int> sessions_;
    // Хранение откликов: ключ - ID объявления, значение - множество ID пользователей
    std::unordered_map<int, std::unordered_set<int>> responses_;
    int nextUserId_ = 1;
    int nextAdvertId_ = 1;

    std::filesystem::path staticRoot_;
};

BulletinBoardApp::BulletinBoardApp()
{
    auto sourceDir = std::filesystem::path(__FILE__).parent_path().parent_path();
    staticRoot_ = std::filesystem::absolute(sourceDir / "public");

    // Create demo users and adverts
    User demo;
    demo.id = nextUserId_++;
    demo.name = "Demo User";
    demo.email = "demo@example.com";
    demo.passwordHash = hashPassword("demo123");
    users_.push_back(demo);
    emailToUserId_[demo.email] = demo.id;

    User alice;
    alice.id = nextUserId_++;
    alice.name = "Alice Smith";
    alice.email = "alice@example.com";
    alice.passwordHash = hashPassword("alice123");
    users_.push_back(alice);
    emailToUserId_[alice.email] = alice.id;

    Advertisement sample;
    sample.id = nextAdvertId_++;
    sample.ownerId = demo.id;
    sample.title = "Vintage Bicycle";
    sample.description = "Reliable city bike. Recently serviced.";
    sample.price = 150.0;
    sample.createdAt = std::time(nullptr);
    adverts_.push_back(sample);

    Advertisement sample2;
    sample2.id = nextAdvertId_++;
    sample2.ownerId = demo.id;
    sample2.title = "Gaming Laptop";
    sample2.description = "15\" display, RTX graphics, 16GB RAM.";
    sample2.price = 950.0;
    sample2.createdAt = std::time(nullptr);
    adverts_.push_back(sample2);

    Advertisement sample3;
    sample3.id = nextAdvertId_++;
    sample3.ownerId = alice.id;
    sample3.title = "iPhone 14 Pro";
    sample3.description = "Mint condition, 256GB, with original box and accessories.";
    sample3.price = 750.0;
    sample3.createdAt = std::time(nullptr);
    adverts_.push_back(sample3);
}

void BulletinBoardApp::run(uint16_t port)
{
    int serverSock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0)
    {
        std::perror("socket");
        return;
    }

    int opt = 1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::perror("setsockopt");
        ::close(serverSock);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(serverSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::perror("bind");
        ::close(serverSock);
        return;
    }

    if (listen(serverSock, kBacklogSize) < 0)
    {
        std::perror("listen");
        ::close(serverSock);
        return;
    }

    std::cout << "BulletinBoard running on http://localhost:" << port << std::endl;
    while (true)
    {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(serverSock, reinterpret_cast<sockaddr *>(&clientAddr), &len);
        if (clientSock < 0)
        {
            std::perror("accept");
            continue;
        }

        std::thread([this, clientSock]()
                    {
            HttpRequest request;
            if (!parseRequest(clientSock, request))
            {
                ::close(clientSock);
                return;
            }

            HttpResponse response;
            routeRequest(request, response);
            sendResponse(clientSock, response);
            ::close(clientSock); })
            .detach();
    }
}

bool BulletinBoardApp::parseRequest(int clientSock, HttpRequest &request) const
{
    std::string raw;
    raw.reserve(1024);
    char buffer[kBufferSize];
    ssize_t received = 0;
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;

    while (true)
    {
        received = ::recv(clientSock, buffer, sizeof(buffer), 0);
        if (received <= 0)
        {
            return false;
        }
        raw.append(buffer, received);
        if (headerEnd == std::string::npos)
        {
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos)
            {
                const auto headerSection = raw.substr(0, headerEnd);
                std::istringstream stream(headerSection);

                std::string startLine;
                std::getline(stream, startLine);
                if (!startLine.empty() && startLine.back() == '\r')
                {
                    startLine.pop_back();
                }

                std::istringstream startLineStream(startLine);
                std::string httpVersion;
                startLineStream >> request.method >> request.rawTarget >> httpVersion;
                if (request.rawTarget.empty())
                {
                    return false;
                }

                const auto question = request.rawTarget.find('?');
                if (question != std::string::npos)
                {
                    request.path = request.rawTarget.substr(0, question);
                    request.query = parseParams(request.rawTarget.substr(question + 1));
                }
                else
                {
                    request.path = request.rawTarget;
                }
                if (request.path.empty())
                {
                    request.path = "/";
                }

                std::string headerLine;
                while (std::getline(stream, headerLine))
                {
                    if (!headerLine.empty() && headerLine.back() == '\r')
                    {
                        headerLine.pop_back();
                    }
                    if (headerLine.empty())
                    {
                        continue;
                    }
                    const auto colon = headerLine.find(':');
                    if (colon == std::string::npos)
                    {
                        continue;
                    }
                    std::string key = toLower(headerLine.substr(0, colon));
                    std::string value = trim(headerLine.substr(colon + 1));
                    request.headers.emplace(std::move(key), std::move(value));
                }

                if (auto it = request.headers.find("content-length"); it != request.headers.end())
                {
                    contentLength = static_cast<size_t>(std::stoul(it->second));
                }
            }
        }

        if (headerEnd != std::string::npos)
        {
            const size_t totalNeeded = headerEnd + 4 + contentLength;
            if (raw.size() >= totalNeeded)
            {
                request.body = raw.substr(headerEnd + 4, contentLength);
                break;
            }
        }
    }

    const auto contentType = request.getHeader("content-type");
    if (!contentType.empty() && contentType.find("application/x-www-form-urlencoded") != std::string::npos)
    {
        request.form = parseParams(request.body);
    }

    return true;
}

void BulletinBoardApp::routeRequest(const HttpRequest &request, HttpResponse &response)
{
    static const std::string apiPrefix = "/api/";
    if (request.path.rfind(apiPrefix, 0) == 0)
    {
        if (!handleApi(request, response))
        {
            response.status = 404;
            response.body = R"({"error":"Endpoint not found"})";
        }
        return;
    }

    if (!serveStatic(request.path, response))
    {
        response.status = 404;
        response.contentType = "text/plain; charset=utf-8";
        response.body = "Not Found";
    }
}

bool BulletinBoardApp::handleApi(const HttpRequest &request, HttpResponse &response)
{
    if (request.method == "POST" && request.path == "/api/register")
    {
        handleRegister(request, response);
        return true;
    }
    if (request.method == "POST" && request.path == "/api/login")
    {
        handleLogin(request, response);
        return true;
    }
    if (request.method == "POST" && request.path == "/api/logout")
    {
        handleLogout(request, response);
        return true;
    }
    if (request.method == "GET" && request.path == "/api/session")
    {
        handleSession(request, response);
        return true;
    }
    if (request.method == "GET" && request.path == "/api/ads")
    {
        handleAdsList(request, response);
        return true;
    }
    if (request.method == "GET" && request.path == "/api/ads/my-responses")
    {
        handleMyResponses(request, response);
        return true;
    }
    if (request.method == "POST" && request.path == "/api/ads")
    {
        handleCreateAd(request, response);
        return true;
    }
    if (request.method == "DELETE" && request.path.rfind("/api/ads/", 0) == 0)
    {
        const std::string idStr = request.path.substr(std::string("/api/ads/").size());
        if (!idStr.empty() && std::all_of(idStr.begin(), idStr.end(), ::isdigit))
        {
            handleDeleteAd(request, response, std::stoi(idStr));
            return true;
        }
    }
    if (request.method == "POST" && request.path.rfind("/api/ads/", 0) == 0)
    {
        const std::string suffix = request.path.substr(std::string("/api/ads/").size());
        const auto slash = suffix.find('/');
        if (slash != std::string::npos)
        {
            const std::string idStr = suffix.substr(0, slash);
            const std::string action = suffix.substr(slash + 1);
            if (!idStr.empty() && std::all_of(idStr.begin(), idStr.end(), ::isdigit) && action == "respond")
            {
                handleRespondToAd(request, response, std::stoi(idStr));
                return true;
            }
        }
    }
    if (request.method == "GET" && request.path.rfind("/api/ads/", 0) == 0)
    {
        const std::string suffix = request.path.substr(std::string("/api/ads/").size());
        const auto slash = suffix.find('/');
        if (slash != std::string::npos)
        {
            const std::string idStr = suffix.substr(0, slash);
            const std::string action = suffix.substr(slash + 1);
            if (!idStr.empty() && std::all_of(idStr.begin(), idStr.end(), ::isdigit) && action == "responders")
            {
                handleAdResponders(request, response, std::stoi(idStr));
                return true;
            }
        }
    }
    return false;
}

bool BulletinBoardApp::serveStatic(const std::string &path, HttpResponse &response) const
{
    std::filesystem::path relative;
    if (path == "/" || path.empty())
    {
        relative = "index.html";
    }
    else
    {
        std::string clean = path;
        if (clean.front() == '/')
        {
            clean.erase(clean.begin());
        }
        relative = clean;
    }

    std::error_code ec;
    std::filesystem::path fullPath = std::filesystem::weakly_canonical(staticRoot_ / relative, ec);
    if (ec || fullPath.string().find(staticRoot_.string()) != 0)
    {
        return false;
    }

    if (!std::filesystem::is_regular_file(fullPath))
    {
        return false;
    }

    auto content = readFileSafely(fullPath);
    if (content.empty() && std::filesystem::file_size(fullPath) > 0)
    {
        return false;
    }

    response.contentType = guessMimeType(fullPath);
    response.body = std::move(content);
    response.setHeader("Cache-Control", "no-cache");
    return true;
}

void BulletinBoardApp::sendResponse(int clientSock, const HttpResponse &response) const
{
    const auto statusText = [status = response.status]()
    {
        switch (status)
        {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 409:
            return "Conflict";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
        }
    }();

    std::ostringstream oss;
    oss << "HTTP/1.1 " << response.status << ' ' << statusText << "\r\n";
    oss << "Content-Type: " << response.contentType << "\r\n";
    oss << "Content-Length: " << response.body.size() << "\r\n";
    oss << "Connection: close\r\n";
    for (const auto &[key, value] : response.headers)
    {
        oss << key << ": " << value << "\r\n";
    }
    oss << "\r\n";
    oss << response.body;

    const auto data = oss.str();
    size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t result = ::send(clientSock, data.data() + sent, data.size() - sent, 0);
        if (result <= 0)
        {
            break;
        }
        sent += static_cast<size_t>(result);
    }
}

std::optional<int> BulletinBoardApp::authenticate(const HttpRequest &request) const
{
    const std::string authHeader = request.getHeader("authorization");
    if (authHeader.size() < 8)
    {
        return std::nullopt;
    }
    const std::string prefix = "bearer ";
    auto lowerAuth = toLower(authHeader.substr(0, prefix.size()));
    if (lowerAuth != prefix)
    {
        return std::nullopt;
    }
    std::string token = trim(authHeader.substr(prefix.size()));
    std::lock_guard lock(dataMutex_);
    if (auto it = sessions_.find(token); it != sessions_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void BulletinBoardApp::handleRegister(const HttpRequest &request, HttpResponse &response)
{
    const auto name = request.getParam("name");
    const auto email = request.getParam("email");
    const auto password = request.getParam("password");
    if (name.empty() || email.empty() || password.empty())
    {
        response.status = 400;
        response.body = R"({"error":"All fields are required"})";
        return;
    }

    std::lock_guard lock(dataMutex_);
    if (emailToUserId_.count(email) > 0)
    {
        response.status = 409;
        response.body = R"({"error":"Email already registered"})";
        return;
    }

    User user;
    user.id = nextUserId_++;
    user.name = name;
    user.email = email;
    user.passwordHash = hashPassword(password);
    users_.push_back(user);
    emailToUserId_[email] = user.id;

    response.body = R"({"success":true,"message":"Registration complete"})";
}

void BulletinBoardApp::handleLogin(const HttpRequest &request, HttpResponse &response)
{
    const auto email = request.getParam("email");
    const auto password = request.getParam("password");
    if (email.empty() || password.empty())
    {
        response.status = 400;
        response.body = R"({"error":"Email and password are required"})";
        return;
    }

    std::lock_guard lock(dataMutex_);
    auto it = emailToUserId_.find(email);
    if (it == emailToUserId_.end())
    {
        response.status = 401;
        response.body = R"({"error":"Invalid credentials"})";
        return;
    }
    const auto userId = it->second;
    const auto &user = users_[userId - 1];
    if (user.passwordHash != hashPassword(password))
    {
        response.status = 401;
        response.body = R"({"error":"Invalid credentials"})";
        return;
    }

    const std::string token = generateToken();
    sessions_[token] = user.id;

    std::ostringstream oss;
    oss << R"({"token":")" << token << R"(","user":)" << userToJson(user) << '}';
    response.body = oss.str();
}

void BulletinBoardApp::handleLogout(const HttpRequest &request, HttpResponse &response)
{
    const auto authHeader = request.getHeader("authorization");
    if (authHeader.size() >= 8)
    {
        const std::string prefix = "bearer ";
        auto lowerAuth = toLower(authHeader.substr(0, prefix.size()));
        if (lowerAuth == prefix)
        {
            const std::string token = trim(authHeader.substr(prefix.size()));
            std::lock_guard lock(dataMutex_);
            sessions_.erase(token);
        }
    }

    response.body = R"({"success":true})";
}

void BulletinBoardApp::handleSession(const HttpRequest &request, HttpResponse &response)
{
    const auto userId = authenticate(request);
    if (!userId)
    {
        response.body = R"({"authenticated":false})";
        return;
    }

    std::lock_guard lock(dataMutex_);
    const User &user = users_[*userId - 1];
    std::ostringstream oss;
    oss << R"({"authenticated":true,"user":)" << userToJson(user) << '}';
    response.body = oss.str();
}

void BulletinBoardApp::handleAdsList(const HttpRequest &request, HttpResponse &response)
{
    const auto userId = authenticate(request);
    response.body = buildAdsJson(userId.value_or(0));
}

void BulletinBoardApp::handleCreateAd(const HttpRequest &request, HttpResponse &response)
{
    const auto userId = authenticate(request);
    if (!userId)
    {
        response.status = 401;
        response.body = R"({"error":"Authentication required"})";
        return;
    }

    const auto title = request.getParam("title");
    const auto description = request.getParam("description");
    const auto priceStr = request.getParam("price");

    if (title.empty() || description.empty())
    {
        response.status = 400;
        response.body = R"({"error":"Title and description are required"})";
        return;
    }

    double price = 0.0;
    if (!priceStr.empty())
    {
        try
        {
            price = std::stod(priceStr);
        }
        catch (...)
        {
            response.status = 400;
            response.body = R"({"error":"Invalid price"})";
            return;
        }
    }

    {
        Advertisement advert;
        advert.id = nextAdvertId_++;
        advert.ownerId = *userId;
        advert.title = title;
        advert.description = description;
        advert.price = price;
        advert.createdAt = std::time(nullptr);

        std::lock_guard lock(dataMutex_);
        adverts_.push_back(std::move(advert));
    }

    response.body = R"({"success":true})";
}

void BulletinBoardApp::handleDeleteAd(const HttpRequest &request, HttpResponse &response, int advertId)
{
    const auto userId = authenticate(request);
    if (!userId)
    {
        response.status = 401;
        response.body = R"({"error":"Authentication required"})";
        return;
    }

    std::lock_guard lock(dataMutex_);
    auto it = std::find_if(adverts_.begin(), adverts_.end(), [advertId](const Advertisement &ad)
                           { return ad.id == advertId; });
    if (it == adverts_.end())
    {
        response.status = 404;
        response.body = R"({"error":"Advertisement not found"})";
        return;
    }
    if (it->ownerId != *userId)
    {
        response.status = 403;
        response.body = R"({"error":"You can only delete your own advertisements"})";
        return;
    }
    adverts_.erase(it);
    // Удаляем также все отклики на это объявление
    responses_.erase(advertId);
    response.body = R"({"success":true})";
}

void BulletinBoardApp::handleRespondToAd(const HttpRequest &request, HttpResponse &response, int advertId)
{
    // Проверка аутентификации
    const auto userId = authenticate(request);
    if (!userId)
    {
        response.status = 401;
        response.body = R"({"error":"Authentication required"})";
        return;
    }

    std::lock_guard lock(dataMutex_);

    // Проверка существования объявления
    auto it = std::find_if(adverts_.begin(), adverts_.end(), [advertId](const Advertisement &ad)
                           { return ad.id == advertId; });
    if (it == adverts_.end())
    {
        response.status = 404;
        response.body = R"({"error":"Advertisement not found"})";
        return;
    }

    // Проверка: пользователь не может откликнуться на своё объявление
    if (it->ownerId == *userId)
    {
        response.status = 400;
        response.body = R"({"error":"You cannot respond to your own advertisement"})";
        return;
    }

    // Проверка: пользователь уже откликался на это объявление
    if (responses_[advertId].count(*userId) > 0)
    {
        response.status = 409;
        response.body = R"({"error":"You have already responded to this advertisement"})";
        return;
    }

    // Добавление отклика
    responses_[advertId].insert(*userId);

    response.body = R"({"success":true})";
}

void BulletinBoardApp::handleMyResponses(const HttpRequest &request, HttpResponse &response)
{
    // Проверка аутентификации
    const auto userId = authenticate(request);
    if (!userId)
    {
        response.status = 401;
        response.body = R"({"error":"Authentication required"})";
        return;
    }

    std::lock_guard lock(dataMutex_);

    // Собираем все объявления, на которые откликнулся пользователь
    std::ostringstream oss;
    oss << R"({"ads":[)";

    bool first = true;
    for (const auto &[adId, userIds] : responses_)
    {
        // Проверяем, откликался ли текущий пользователь на это объявление
        if (userIds.count(*userId) > 0)
        {
            // Находим само объявление
            auto adIt = std::find_if(adverts_.begin(), adverts_.end(),
                                     [adId](const Advertisement &ad)
                                     { return ad.id == adId; });

            if (adIt != adverts_.end())
            {
                const auto &ad = *adIt;
                const auto &owner = users_[ad.ownerId - 1];

                if (!first)
                {
                    oss << ',';
                }
                first = false;

                oss << '{';
                oss << R"("id":)" << ad.id << ',';
                oss << R"("title":")" << jsonEscape(ad.title) << R"(",)";
                oss << R"("description":")" << jsonEscape(ad.description) << R"(",)";
                {
                    std::ostringstream priceStream;
                    priceStream << std::fixed << std::setprecision(2) << ad.price;
                    oss << R"("price":)" << priceStream.str() << ',';
                }
                oss << R"("ownerName":")" << jsonEscape(owner.name) << R"(",)";
                oss << R"("createdAt":)" << static_cast<long long>(ad.createdAt) << ',';
                oss << R"("hasResponded":true)";
                oss << '}';
            }
        }
    }

    oss << "]}";
    response.body = oss.str();
}

void BulletinBoardApp::handleAdResponders(const HttpRequest &request, HttpResponse &response, int advertId)
{
    // Проверка аутентификации
    const auto userId = authenticate(request);
    if (!userId)
    {
        response.status = 401;
        response.body = R"({"error":"Authentication required"})";
        return;
    }

    std::lock_guard lock(dataMutex_);

    // Находим объявление
    auto adIt = std::find_if(adverts_.begin(), adverts_.end(),
                             [advertId](const Advertisement &ad)
                             { return ad.id == advertId; });

    if (adIt == adverts_.end())
    {
        response.status = 404;
        response.body = R"({"error":"Advertisement not found"})";
        return;
    }

    // Проверяем, что запрашивающий является автором объявления
    if (adIt->ownerId != *userId)
    {
        response.status = 403;
        response.body = R"({"error":"Only the owner can view responders"})";
        return;
    }

    // Собираем список пользователей, откликнувшихся на это объявление
    std::ostringstream oss;
    oss << R"({"responders":[)";

    auto responsesIt = responses_.find(advertId);
    if (responsesIt != responses_.end())
    {
        bool first = true;
        for (int responderId : responsesIt->second)
        {
            // Находим пользователя
            auto userIt = std::find_if(users_.begin(), users_.end(),
                                       [responderId](const User &u)
                                       { return u.id == responderId; });

            if (userIt != users_.end())
            {
                if (!first)
                {
                    oss << ',';
                }
                first = false;

                oss << '{';
                oss << R"("id":)" << userIt->id << ',';
                oss << R"("name":")" << jsonEscape(userIt->name) << R"(",)";
                oss << R"("email":")" << jsonEscape(userIt->email) << '"';
                oss << '}';
            }
        }
    }

    oss << "]}";
    response.body = oss.str();
}

std::string BulletinBoardApp::readFileSafely(const std::filesystem::path &path) const
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    std::ostringstream oss;
    oss << input.rdbuf();
    return oss.str();
}

std::string BulletinBoardApp::guessMimeType(const std::filesystem::path &path) const
{
    const auto ext = path.extension().string();
    if (ext == ".html")
        return "text/html; charset=utf-8";
    if (ext == ".css")
        return "text/css; charset=utf-8";
    if (ext == ".js")
        return "application/javascript; charset=utf-8";
    if (ext == ".json")
        return "application/json; charset=utf-8";
    if (ext == ".png")
        return "image/png";
    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";
    if (ext == ".svg")
        return "image/svg+xml";
    if (ext == ".ico")
        return "image/x-icon";
    return "text/plain; charset=utf-8";
}

std::string BulletinBoardApp::buildAdsJson(int currentUserId) const
{
    std::lock_guard lock(dataMutex_);
    std::ostringstream oss;
    oss << R"({"ads":[)";
    for (size_t i = 0; i < adverts_.size(); ++i)
    {
        const auto &ad = adverts_[i];
        const auto &owner = users_[ad.ownerId - 1];
        if (i > 0)
        {
            oss << ',';
        }
        oss << '{';
        oss << R"("id":)" << ad.id << ',';
        oss << R"("title":")" << jsonEscape(ad.title) << R"(",)";
        oss << R"("description":")" << jsonEscape(ad.description) << R"(",)";
        {
            std::ostringstream priceStream;
            priceStream << std::fixed << std::setprecision(2) << ad.price;
            oss << R"("price":)" << priceStream.str() << ',';
        }
        oss << R"("ownerName":")" << jsonEscape(owner.name) << R"(",)";
        oss << R"("createdAt":)" << static_cast<long long>(ad.createdAt) << ',';
        oss << R"("mine":)" << (ad.ownerId == currentUserId ? "true" : "false") << ',';

        // Информация об откликах
        const bool isOwner = (ad.ownerId == currentUserId);
        auto responsesIt = responses_.find(ad.id);
        const size_t responsesCount = (responsesIt != responses_.end()) ? responsesIt->second.size() : 0;

        // Только автор видит количество откликов
        if (isOwner)
        {
            oss << R"("responsesCount":)" << responsesCount << ',';
        }

        // Проверка: откликался ли текущий пользователь
        bool hasResponded = false;
        if (currentUserId > 0 && responsesIt != responses_.end())
        {
            hasResponded = responsesIt->second.count(currentUserId) > 0;
        }
        oss << R"("hasResponded":)" << (hasResponded ? "true" : "false");

        oss << '}';
    }
    oss << "]}";
    return oss.str();
}

std::string BulletinBoardApp::userToJson(const User &user) const
{
    std::ostringstream oss;
    oss << '{'
        << R"("id":)" << user.id << ','
        << R"("name":")" << jsonEscape(user.name) << R"(",)"
        << R"("email":")" << jsonEscape(user.email) << "\"}";
    return oss.str();
}

std::string BulletinBoardApp::hashPassword(const std::string &password) const
{
    std::hash<std::string> hasher;
    std::size_t hashed = hasher(password);
    std::ostringstream oss;
    oss << std::hex << hashed;
    return oss.str();
}

std::string BulletinBoardApp::generateToken() const
{
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    std::ostringstream oss;
    for (int i = 0; i < 32; ++i)
    {
        oss << std::hex << dist(rng);
    }
    return oss.str();
}

int main()
{
    BulletinBoardApp app;
    app.run(8080);
    return 0;
}
