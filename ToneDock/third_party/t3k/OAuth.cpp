#include "OAuth.h"
#include <stdexcept>
#if JUCE_WINDOWS
#include <windows.h>
#include <bcrypt.h>
#elif JUCE_MAC
#include <Security/Security.h>
#else
#include <sys/random.h>
#include <cerrno>
#endif

namespace t3k::oauth
{
    namespace
    {
        juce::String base64Url (const void* data, size_t size)
        {
            auto encoded = juce::Base64::toBase64 (data, size);
            return encoded.replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
        }

        juce::String randomUrlSafeString (int numBytes)
        {
            juce::MemoryBlock bytes ((size_t) numBytes);
#if JUCE_WINDOWS
            if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(bytes.getData()), ULONG(numBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
                throw std::runtime_error("Secure random generator unavailable");
#elif JUCE_MAC
            if (SecRandomCopyBytes(kSecRandomDefault,bytes.getSize(),static_cast<uint8_t*>(bytes.getData())) != errSecSuccess)
                throw std::runtime_error("Secure random generator unavailable");
#else
            size_t offset=0;
            while(offset<bytes.getSize()) {
                auto count=getrandom(static_cast<char*>(bytes.getData())+offset,bytes.getSize()-offset,0);
                if(count<0 && errno==EINTR)continue;
                if(count<=0)throw std::runtime_error("Secure random generator unavailable");
                offset+=size_t(count);
            }
#endif
            return base64Url (bytes.getData(), bytes.getSize());
        }
    }

    Pkce Pkce::generate()
    {
        Pkce p;
        p.codeVerifier = randomUrlSafeString (32);          // 43 chars after base64url
        juce::SHA256 hash (p.codeVerifier.toRawUTF8(), p.codeVerifier.getNumBytesAsUTF8());
        auto raw = hash.getRawData();
        p.codeChallenge = base64Url (raw.getData(), raw.getSize());
        p.state = randomUrlSafeString (16);
        return p;
    }

    juce::URL authorizeUrl (const juce::String& apiOrigin,
                            const juce::String& publishableKey,
                            const juce::String& redirectUri,
                            const Pkce& pkce,
                            const juce::StringPairArray& extra)
    {
        auto url = juce::URL (apiOrigin + "/api/v1/oauth/authorize")
                       .withParameter ("client_id", publishableKey)
                       .withParameter ("redirect_uri", redirectUri)
                       .withParameter ("response_type", "code")
                       .withParameter ("code_challenge", pkce.codeChallenge)
                       .withParameter ("code_challenge_method", "S256")
                       .withParameter ("state", pkce.state);
        for (auto& key : extra.getAllKeys())
            url = url.withParameter (key, extra[key]);
        return url;
    }

    Callback Callback::parse (const juce::String& queryOrUrl)
    {
        auto query = queryOrUrl.contains ("?") ? queryOrUrl.fromFirstOccurrenceOf ("?", false, false) : queryOrUrl;
        query = query.upToFirstOccurrenceOf ("#", false, false);

        Callback cb;
        for (auto& pair : juce::StringArray::fromTokens (query, "&", {}))
        {
            auto key = juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false));
            auto value = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' '));
            if (key == "code") cb.code = value;
            else if (key == "state") cb.state = value;
            else if (key == "error") cb.error = value;
            else if (key == "tone_id") cb.toneId = value;
            else if (key == "canceled") cb.canceled = (value == "true");
        }
        return cb;
    }
}
