#include "Http.h"

namespace t3k
{
    Http::Http() = default;

    Http::~Http()
    {
        pool.removeAllJobs (true, -1);
    }

    void Http::send (HttpRequest request, std::function<void (HttpResponse)> done)
    {
        pool.addJob ([req = std::move (request), cb = std::move (done)]
        {
            auto response = sendSync (req);
            juce::MessageManager::callAsync ([cb, res = std::move (response)]() mutable
            {
                cb (std::move (res));
            });
        });
    }

    HttpResponse Http::sendSync (const HttpRequest& request)
    {
        HttpResponse response;

        juce::String headerBlock;
        for (auto& key : request.headers.getAllKeys())
            headerBlock << key << ": " << request.headers[key] << "\r\n";

        auto url = request.url;
        const bool hasBody = request.method == "POST" || request.method == "PUT";
        if (hasBody)
            url = url.withPOSTData (request.body);

        juce::StringPairArray responseHeaders;
        int status = 0;

        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withHttpRequestCmd (request.method)
                           .withExtraHeaders (headerBlock)
                           .withConnectionTimeoutMs (request.timeoutMs)
                           .withNumRedirectsToFollow (0)
                           .withStatusCode (&status)
                           .withResponseHeaders (&responseHeaders);

        if (auto stream = url.createInputStream (options))
        {
            stream->readIntoMemoryBlock (response.body,64*1024*1024+1);
            if(response.body.getSize()>64*1024*1024) {
                response.body.reset(); response.error="Response exceeds 64 MiB limit";return response;
            }
            response.status = status;
            for (auto& key : responseHeaders.getAllKeys())
                response.headers.set (key.toLowerCase(), responseHeaders[key]);
        }

        else
        {
            response.status = status;
            if (status == 0) response.error = "Could not reach " + request.url.getDomain();
            for (auto& key : responseHeaders.getAllKeys()) response.headers.set(key.toLowerCase(),responseHeaders[key]);
        }

        // Explicit redirects: never forward a Bearer token to another origin.
        if(response.status>=300 && response.status<400) {
            static thread_local int redirectDepth=0;
            auto location=response.header("location");
            if(location.isNotEmpty() && redirectDepth<5) {
                auto redirect=request;
                if(location.startsWithChar('/')) location=request.url.getScheme()+"://"+request.url.getDomain()+location;
                redirect.url=juce::URL(location);
                if(redirect.url.getScheme()!="https") {response.error="Refused insecure redirect";response.status=0;return response;}
                if(redirect.url.getDomain()!=request.url.getDomain() || redirect.url.getPort()!=request.url.getPort()) redirect.headers.remove("Authorization");
                ++redirectDepth;auto result=sendSync(redirect);--redirectDepth;return result;
            }
        }
        return response;
    }
}
