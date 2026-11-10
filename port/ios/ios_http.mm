/*
 * iOS: the PC services' HTTP request (port/pc/pc_http.h) through
 * NSURLSession, in WinHTTP's and libcurl's place. The same contract:
 * redirects followed, the body kept to maxReply or streamed to the sink,
 * HttpError{0} when nothing answered, {-1} when the sink stopped.
 *
 * The calling thread waits; the session's delegate runs on its own queue.
 */
#import <Foundation/Foundation.h>

#include "../pc/pc_http.h"

#include <algorithm>

struct IosHttpState {
    const pcnet::HttpRequest *req;
    pcnet::HttpReply reply;
    bool stopped = false;
    std::string error;
    bool failed = false;
    dispatch_semaphore_t done;
};

@interface GevrHttpDelegate : NSObject <NSURLSessionDataDelegate>
@property (nonatomic, assign) IosHttpState *state;
@end

@implementation GevrHttpDelegate
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))handler
{
    NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
    _state->reply.status = (int)http.statusCode;
    _state->reply.length = response.expectedContentLength;   // -1 when not given
    handler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data
{
    const pcnet::HttpRequest &r = *_state->req;
    __block bool keep = true;
    [data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
        // a streamed body only while the answer is a success: an error page is kept as text
        if (r.sink && _state->reply.status < 400) {
            if (!r.sink((const char *)bytes, range.length)) {
                keep = false;
                *stop = YES;
            }
        } else if (_state->reply.body.size() < r.maxReply) {
            _state->reply.body.append((const char *)bytes,
                                      std::min<size_t>(range.length, r.maxReply - _state->reply.body.size()));
        }
    }];
    if (!keep) {
        _state->stopped = true;
        [task cancel];
    }
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    if (error && !_state->stopped) {
        _state->failed = true;
        _state->error = error.localizedDescription.UTF8String ?: "no network";
    }
    dispatch_semaphore_signal(_state->done);
}
@end

namespace pcnet {

HttpReply httpSendApple(const HttpRequest &r)
{
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:r.url.c_str()]];
        if (!url) throw HttpError{0, "bad address"};
        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
        req.HTTPMethod = [NSString stringWithUTF8String:r.method.c_str()];
        [req setValue:[NSString stringWithUTF8String:r.agent.c_str()] forHTTPHeaderField:@"User-Agent"];
        for (const std::string &h : r.headers) {
            const size_t colon = h.find(':');
            if (colon == std::string::npos) continue;
            std::string value = h.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            [req setValue:[NSString stringWithUTF8String:value.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:h.substr(0, colon).c_str()]];
        }
        if (r.body) req.HTTPBody = [NSData dataWithBytes:r.body->data() length:r.body->size()];

        NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        // WinHTTP's read timeout is per read; NSURLSession's request timeout is the gap between packets
        cfg.timeoutIntervalForRequest = std::max(r.connectMs, r.readMs) / 1000.0;
        IosHttpState st;
        st.req = &r;
        st.done = dispatch_semaphore_create(0);
        GevrHttpDelegate *del = [GevrHttpDelegate new];
        del.state = &st;
        NSOperationQueue *queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg delegate:del delegateQueue:queue];
        [[session dataTaskWithRequest:req] resume];
        dispatch_semaphore_wait(st.done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
        if (st.stopped) throw HttpError{-1, "cancelled"};
        if (st.failed) throw HttpError{0, st.error};
        return st.reply;
    }
}

}
