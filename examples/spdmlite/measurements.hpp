#pragma once
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
static constexpr auto MEASUREMENT_EVENT = "Measurement";
static constexpr auto MEASUREMENT_FAILED_EVENT = "MeasurementFailed";
static constexpr auto MEASUREMENT_REQUEST_EVENT = "MeasurementRequest";
static constexpr auto FETCH_MEASUREMENT_EVENT = "FetchMeasurement";

std::string getExecutableMeasurement(const std::string& exePath,
                                     const std::string& privKeyPath)
{
    // Open the executable file
    std::ifstream file(exePath, std::ios::binary);
    if (!file)
        return {};

    // Read the entire file into a buffer
    std::vector<unsigned char> fileData((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());

    // Load private key
    EVP_PKEY* pkey = nullptr;
    {
        BIO* keybio = BIO_new_file(privKeyPath.data(), "r");
        if (!keybio)
            return {};
        pkey = PEM_read_bio_PrivateKey(keybio, nullptr, nullptr, nullptr);
        BIO_free(keybio);
        if (!pkey)
            return {};
    }

    // Sign the file using EVP_DigestSign* APIs
    std::string signature;
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx)
    {
        EVP_PKEY_free(pkey);
        return {};
    }

    if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1)
    {
        if (EVP_DigestSignUpdate(mdctx, fileData.data(), fileData.size()) == 1)
        {
            size_t sigLen = 0;
            if (EVP_DigestSignFinal(mdctx, nullptr, &sigLen) == 1)
            {
                std::vector<unsigned char> sig(sigLen);
                if (EVP_DigestSignFinal(mdctx, sig.data(), &sigLen) == 1)
                {
                    std::ostringstream oss;
                    for (size_t i = 0; i < sigLen; ++i)
                    {
                        oss << std::hex << std::setw(2) << std::setfill('0')
                            << static_cast<int>(sig[i]);
                    }
                    signature = oss.str();
                }
            }
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return signature;
}
bool verifyExecutableMeasurement(const std::string& exePath,
                                 const std::string& pubKeyPath,
                                 const std::string& signatureHex)
{
    // Open the executable file
    std::ifstream file(exePath, std::ios::binary);
    if (!file)
        return false;

    // Read the entire file into a buffer
    std::vector<unsigned char> fileData((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());

    // Convert hex signature to binary
    if (signatureHex.length() % 2 != 0)
        return false;
    std::vector<unsigned char> signature(signatureHex.length() / 2);
    for (size_t i = 0; i < signature.size(); ++i)
    {
        unsigned int byte;
        std::istringstream iss(signatureHex.substr(2 * i, 2));
        iss >> std::hex >> byte;
        signature[i] = static_cast<unsigned char>(byte);
    }

    // Load public key
    EVP_PKEY* pkey = nullptr;
    {
        BIO* keybio = BIO_new_file(pubKeyPath.data(), "r");
        if (!keybio)
            return false;
        pkey = PEM_read_bio_PUBKEY(keybio, nullptr, nullptr, nullptr);
        BIO_free(keybio);
        if (!pkey)
            return false;
    }

    // Use EVP_DigestVerify* APIs to verify the signature
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx)
    {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool result = false;
    if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1)
    {
        if (EVP_DigestVerifyUpdate(mdctx, fileData.data(), fileData.size()) ==
            1)
        {
            if (EVP_DigestVerifyFinal(mdctx, signature.data(),
                                      signature.size()) == 1)
            {
                result = true;
            }
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return result;
}
struct MeasurementTaker
{
    std::string privkey;
    MeasurementTaker(const std::string& privkeyPath) : privkey(privkeyPath) {}
    std::string operator()(const std::string& exePath)
    {
        return getExecutableMeasurement(exePath, privkey);
    }
};
struct MeasurementVerifier
{
    std::string pubkey;
    MeasurementVerifier(const std::string& pubKey) : pubkey(pubKey) {}
    bool operator()(const std::string& exePath, const std::string& measurement)
    {
        return verifyExecutableMeasurement(exePath, pubkey, measurement);
    }
};
struct MeasurementHandler
{
    MeasurementTaker measurementTaker;
    MeasurementVerifier measurementVerifier;
    EventQueue& eventQueue;
    net::io_context& ioContext;
    std::vector<std::string> toMeasure;
    using MeasurementResult = std::map<std::string, bool>;
    MeasurementResult measurements;
    using MeasurementCallback = std::function<void(MeasurementResult)>;
    MeasurementCallback measurementCallback;

    MeasurementHandler(const std::string& privkeyPath,
                       const std::string& pubKeyPath, EventQueue& eventQueue,
                       net::io_context& ioContext) :
        measurementTaker(privkeyPath), measurementVerifier(pubKeyPath),
        eventQueue(eventQueue), ioContext(ioContext)
    {
        eventQueue.addEventProvider(
            FETCH_MEASUREMENT_EVENT,
            std::bind_front(&MeasurementHandler::takeMeasurementProvider,
                            this));
        eventQueue.addEventConsumer(
            FETCH_MEASUREMENT_EVENT,
            std::bind_front(&MeasurementHandler::takeMeasurementConsumer,
                            this));
        eventQueue.addEventConsumer(
            MEASUREMENT_REQUEST_EVENT,
            std::bind_front(&MeasurementHandler::startMeasurementCosumer,
                            this));
    }
    MeasurementHandler(const MeasurementHandler&) = delete;
    MeasurementHandler& operator=(const MeasurementHandler&) = delete;
    MeasurementHandler& addToMeasure(const std::string& exePath)
    {
        toMeasure.push_back(exePath);
        return *this;
    }
    void onMeasurementDone()
    {
        LOG_INFO("Measurement done for all executables");
        measurementCallback(std::move(measurements));
    }
    void afterMeasurementDone(MeasurementCallback&& callback)
    {
        measurementCallback = std::move(callback);
    }
    net::awaitable<MeasurementResult> getMeasurementResult()
    {
        auto h =
            make_awaitable_handler<MeasurementResult>([this](auto promise) {
                afterMeasurementDone(
                    [promise = std::make_shared<decltype(promise)>(
                         std::move(promise))](MeasurementResult m) {
                        promise->setValues(boost::system::error_code{}, m);
                    });
            });
        auto [ec, m] = co_await h();
        co_return m;
    }
    void updateResult(const std::string& exePath, bool result)
    {
        measurements[exePath] = result;
        if (measurements.size() == toMeasure.size())
        {
            ioContext.post(
                std::bind_front(&MeasurementHandler::onMeasurementDone, this));
        }
    }
    net::awaitable<boost::system::error_code> startMeasurementCosumer(
        Streamer streamer, const std::string& eventReplay)
    {
        LOG_DEBUG("Received event: {}", eventReplay);
        auto [id, body] = parseEvent(eventReplay);
        if (id == MEASUREMENT_REQUEST_EVENT)
        {
            LOG_DEBUG("Starting measurement consumer for: {}", body);
            measurements = MeasurementResult{};
            for (const auto& exePath : toMeasure)
            {
                nlohmann::json jsonBody;
                jsonBody["bin"] = exePath;
                auto replay =
                    makeEvent(FETCH_MEASUREMENT_EVENT, jsonBody.dump());
                // co_await sendHeader(streamer, replay);
                eventQueue.addEvent(replay);
            }
            co_return boost::system::error_code{};
        }
        LOG_ERROR("Failed to start measurement consumer: {}", eventReplay);
        co_return boost::system::error_code{};
    }
    net::awaitable<boost::system::error_code> takeMeasurementProvider(
        Streamer streamer, const std::string& eventReplay)
    {
        LOG_DEBUG("Received event: {}", eventReplay);
        auto [id, body] = parseEvent(eventReplay);
        if (id == MEASUREMENT_EVENT)
        {
            LOG_DEBUG("Received measurement event: {}", body);
            auto jsonBody = nlohmann::json::parse(body);
            auto bin = jsonBody.value("bin", std::string{});
            auto measurement = jsonBody.value("measurement", std::string{});
            if (!measurement.empty())
            {
                nlohmann::json jsonMeasurement;
                auto result = measurementVerifier(bin, measurement);
                LOG_DEBUG("Verification result for measurement: {}",
                          result ? "Success" : "Failure");
                updateResult(bin, result);
                co_return boost::system::error_code{};
            }
            updateResult(bin, false);
            co_return boost::system::error_code{};
        }
        LOG_DEBUG("Measurement Failed {}", eventReplay);
        co_return boost::system::error_code{};
    }
    net::awaitable<boost::system::error_code> takeMeasurementConsumer(
        Streamer streamer, const std::string& event)
    {
        LOG_DEBUG("Received event: {}", event);
        auto [id, body] = parseEvent(event);
        if (id == FETCH_MEASUREMENT_EVENT)
        {
            auto jsonBody = nlohmann::json::parse(body);
            auto path = jsonBody["bin"].get<std::string>();
            auto measurement = measurementTaker(path);
            nlohmann::json jsonMeasurement;
            jsonMeasurement["bin"] = path;
            jsonMeasurement["measurement"] = measurement;
            std::string id_toSend = MEASUREMENT_EVENT;
            auto replay = makeEvent(id_toSend, jsonMeasurement.dump());
            co_await sendHeader(streamer, replay);
        }
        co_return boost::system::error_code{};
    }
    void sendMyMeasurement()
    {
        auto replay = makeEvent(MEASUREMENT_REQUEST_EVENT, "SPDM");
        eventQueue.addEvent(replay);
    }
    void waitForRemoteMeasurements(
        std::function<void(MeasurementResult)> onMeasurementDone)
    {
        net::co_spawn(
            ioContext,
            [&, onMeasurementDone =
                    std::move(onMeasurementDone)]() -> net::awaitable<void> {
                while (true)
                {
                    auto measurements = co_await getMeasurementResult();
                    onMeasurementDone(std::move(measurements));
                }
            },
            net::detached);
    }
};
