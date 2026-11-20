/*
 * Apple TV: the launcher's file pickers (port/pc/pc_launcher.cpp pcChooseFile,
 * through gevrIosChooseFile as on iOS, port/ios/ios_picker.m).
 *
 * A TV has no document picker and no Files app, so the file comes from a phone
 * or computer on the same network, the way Apple TV emulators take their
 * games: a dialog shows this TV's address, and a small page served from it
 * there sends the chosen file back, one POST of the raw file (no multipart to
 * parse). The server and the dialog live only while a picker is open. The file
 * lands in the app's temporary folder under its own name, and the caller
 * copies or reads it as it does iOS's picked copy. The previous pick's file
 * goes when the next picker opens: the XBLA import reads its file after the
 * picker has returned.
 *
 * The dialog waits on the game's thread (the main thread) while SDL's event
 * pump keeps UIKit's run loop turning, as ios_picker.m does. Controller
 * presses reach UIKit only while it is up (port/ios/ios_app.m keeps them the
 * game's otherwise), so the remote or a pad can press Cancel.
 */
#import <UIKit/UIKit.h>
#import <CoreImage/CoreImage.h>

#include <SDL.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" void sysLogPrintf(int level, const char *fmt, ...);

namespace {

constexpr int kPort = 8007;              // tried first; any free port if it is taken
constexpr size_t kMaxHeader = 16384;
constexpr int kIdleSeconds = 30;         // a sender that goes quiet this long is dropped

struct Receiver {
    int fd = -1;                          // the listening socket
    int port = 0;
    std::string folder;                   // where a sent file lands
    std::string title;                    // the picker's, for the page
    std::atomic<bool> stop{false};
    std::atomic<bool> receiving{false};   // one file at a time
    std::atomic<int> state{0};            // 0 waiting, 1 a file arrived whole
    std::atomic<int> live{0};             // connections still being served
    std::atomic<long long> got{0}, total{0};
    std::mutex lock;                      // name and path
    std::string name, path;
    std::thread listener;
};

bool sendAll(int fd, const char *p, size_t n)
{
    while (n > 0) {
        const ssize_t k = send(fd, p, n, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        p += k;
        n -= (size_t)k;
    }
    return true;
}

void reply(int fd, const char *status, const char *type, const std::string &body)
{
    char head[256];
    const int n = snprintf(head, sizeof(head),
                           "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                           status, type, body.size());
    if (n > 0 && sendAll(fd, head, (size_t)n)) sendAll(fd, body.data(), body.size());
}

std::string htmlEscape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && hexDigit(s[i + 1]) >= 0 && hexDigit(s[i + 2]) >= 0) {
            out += (char)(hexDigit(s[i + 1]) * 16 + hexDigit(s[i + 2]));
            i += 2;
        } else if (s[i] == '&') {
            break;   // the name is the query's only field
        } else {
            out += s[i] == '+' ? ' ' : s[i];
        }
    }
    return out;
}

/* The sender's file name, kept to what is safe in one folder: no path, no
 * control characters, no leading dots. The extension stays (the XBLA import
 * reads .7z and .zip by it). */
std::string safeName(const std::string &raw)
{
    std::string base = raw;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    std::string out;
    for (unsigned char c : base) {
        if (c < 0x20 || c == 0x7f || c == ':') continue;
        out += (char)c;
    }
    while (!out.empty() && (out[0] == '.' || out[0] == ' ')) out.erase(0, 1);
    if (out.size() > 200) out = out.substr(out.size() - 200);
    return out.empty() ? std::string("upload.bin") : out;
}

std::string page(const std::string &title)
{
    std::string html = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GoldenEye VR on Apple TV</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#0e1628;color:#e6e9ef;font:17px/1.45 -apple-system,system-ui,"Segoe UI",sans-serif}
main{max-width:34em;margin:0 auto;padding:28px 16px}
h1{color:#ffd678;font-size:1.35em;margin:0 0 .4em}
.ask{font-size:1.1em}
label{display:inline-block;margin:.6em 0;padding:.7em 1.2em;border-radius:10px;background:#dea83a;color:#0e1628;font-weight:600;cursor:pointer}
input{display:none}
#bar{height:10px;border-radius:5px;background:#26314a;overflow:hidden;margin:1em 0 .4em}
#fill{height:100%;width:0;background:#dea83a}
small{color:#9aa3b5}
</style></head><body><main>
<h1>GoldenEye VR on Apple TV</h1>
<p class="ask">The Apple TV asks: <b>@TITLE@</b></p>
<label>Choose the file and send it<input type="file" id="file"></label>
<div id="bar"><div id="fill"></div></div>
<p id="msg"></p>
<p><small>This page works while the dialog is open on the TV. Your file goes straight to the Apple TV on your network.</small></p>
</main><script>
var file = document.getElementById('file'), fill = document.getElementById('fill'), msg = document.getElementById('msg');
file.onchange = function () {
  var f = file.files[0];
  if (!f) return;
  var x = new XMLHttpRequest();
  x.open('POST', '/upload?name=' + encodeURIComponent(f.name));
  x.upload.onprogress = function (e) {
    if (!e.lengthComputable) return;
    var pct = Math.floor(100 * e.loaded / e.total);
    fill.style.width = pct + '%';
    msg.textContent = 'Sending ' + f.name + ': ' + pct + '%';
  };
  x.onload = function () { msg.textContent = x.responseText; };
  x.onerror = function () { msg.textContent = 'Sending failed. Is the dialog still open on the TV?'; };
  msg.textContent = 'Sending ' + f.name + '...';
  x.send(f);
  file.value = '';
};
</script></body></html>
)HTML";
    const size_t at = html.find("@TITLE@");
    if (at != std::string::npos) html.replace(at, 7, htmlEscape(title));
    return html;
}

/* Waits for bytes, a second at a time so a stop is noticed; false when the
 * sender closed, failed or went quiet for kIdleSeconds. */
ssize_t recvSome(Receiver &r, int c, char *buf, size_t len)
{
    for (int idle = 0; idle < kIdleSeconds && !r.stop; idle++) {
        const ssize_t k = recv(c, buf, len, 0);
        if (k >= 0) return k;
        if (errno == EINTR) {
            idle--;
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    }
    return -1;
}

void receiveFile(Receiver &r, int c, const std::string &target, long long length, std::string body)
{
    if (length <= 0) {
        reply(c, "411 Length Required", "text/plain; charset=utf-8", "No file arrived.\n");
        return;
    }
    if (r.state.load()) {
        reply(c, "409 Conflict", "text/plain; charset=utf-8",
              "The Apple TV already has a file for this. Choose again on the TV to send another.\n");
        return;
    }
    bool idle = false;
    if (!r.receiving.compare_exchange_strong(idle, true)) {
        reply(c, "409 Conflict", "text/plain; charset=utf-8", "Another file is on its way to the Apple TV.\n");
        return;
    }
    const std::string name = safeName(urlDecode(target.substr(strlen("/upload?name="))));
    const std::string path = r.folder + "/" + name;
    const std::string part = path + ".part";
    {
        std::lock_guard<std::mutex> g(r.lock);
        r.name = name;
    }
    r.got = 0;
    r.total = length;
    FILE *out = fopen(part.c_str(), "wb");
    bool ok = out != nullptr;
    long long got = 0;
    auto take = [&](const char *p, size_t n) {
        if (got + (long long)n > length) n = (size_t)(length - got);
        if (ok && n && fwrite(p, 1, n, out) != n) ok = false;
        got += (long long)n;
        r.got = got;
    };
    take(body.data(), body.size());
    std::vector<char> buf(1 << 16);
    while (ok && got < length && !r.stop) {
        const ssize_t k = recvSome(r, c, buf.data(), buf.size());
        if (k <= 0) {
            ok = false;
            break;
        }
        take(buf.data(), (size_t)k);
    }
    if (out && fclose(out) != 0) ok = false;
    if (!ok || got < length || rename(part.c_str(), path.c_str()) != 0) {
        remove(part.c_str());
        sysLogPrintf(1, "tvos: a file stopped after %lld of %lld bytes", got, length);
        r.got = 0;
        r.total = 0;
        r.receiving = false;
        reply(c, "500 Internal Server Error", "text/plain; charset=utf-8",
              out ? "The file did not arrive whole. Try again.\n" : "The Apple TV could not store the file.\n");
        return;
    }
    {
        std::lock_guard<std::mutex> g(r.lock);
        r.path = path;
    }
    r.state = 1;
    sysLogPrintf(0, "tvos: a file arrived (%lld bytes)", length);
    reply(c, "200 OK", "text/plain; charset=utf-8", "Done: the Apple TV has " + name + ".\n");
}

/* One connection: the page, or a file sent to /upload?name=... */
void serve(Receiver &r, int c)
{
    int one = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    struct timeval second = { 1, 0 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &second, sizeof(second));
    std::string head;
    char buf[8192];
    size_t end = std::string::npos;
    while (end == std::string::npos && head.size() < kMaxHeader) {
        const ssize_t k = recvSome(r, c, buf, sizeof(buf));
        if (k <= 0) return;
        head.append(buf, (size_t)k);
        end = head.find("\r\n\r\n");
    }
    if (end == std::string::npos) {
        reply(c, "431 Request Header Fields Too Large", "text/plain; charset=utf-8", "Too large.\n");
        return;
    }
    std::string body = head.substr(end + 4);   // the body's start, read with the header
    head.resize(end);
    const std::string line = head.substr(0, head.find("\r\n"));
    const size_t sp1 = line.find(' '), sp2 = sp1 == std::string::npos ? sp1 : line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        reply(c, "400 Bad Request", "text/plain; charset=utf-8", "Bad request.\n");
        return;
    }
    const std::string method = line.substr(0, sp1), target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string lower = head;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return (char)tolower(ch); });
    long long length = -1;
    const size_t at = lower.find("\r\ncontent-length:");
    if (at != std::string::npos) length = strtoll(head.c_str() + at + strlen("\r\ncontent-length:"), nullptr, 10);

    if (method == "GET" && (target == "/" || target.rfind("/?", 0) == 0)) {
        reply(c, "200 OK", "text/html; charset=utf-8", page(r.title));
    } else if (method == "POST" && target.rfind("/upload?name=", 0) == 0) {
        receiveFile(r, c, target, length, std::move(body));
    } else {
        reply(c, "404 Not Found", "text/plain; charset=utf-8", "Not here.\n");
    }
}

void acceptLoop(Receiver *r)
{
    while (!r->stop) {
        pollfd p = { r->fd, POLLIN, 0 };
        if (poll(&p, 1, 200) <= 0) continue;
        const int c = accept(r->fd, nullptr, nullptr);
        if (c < 0) continue;
        // each connection on its own thread: a browser's idle spare connection
        // must not hold up the one carrying the file
        r->live++;
        std::thread([r, c] {
            serve(*r, c);
            shutdown(c, SHUT_WR);
            close(c);
            r->live--;
        }).detach();
    }
}

bool start(Receiver &r)
{
    r.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (r.fd < 0) return false;
    int one = 1;
    setsockopt(r.fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_len = sizeof(a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(kPort);
    if (bind(r.fd, (sockaddr *)&a, sizeof(a)) != 0) {
        a.sin_port = 0;   // taken: any free one
        if (bind(r.fd, (sockaddr *)&a, sizeof(a)) != 0) {
            close(r.fd);
            r.fd = -1;
            return false;
        }
    }
    socklen_t len = sizeof(a);
    getsockname(r.fd, (sockaddr *)&a, &len);
    r.port = ntohs(a.sin_port);
    if (::listen(r.fd, 8) != 0) {
        close(r.fd);
        r.fd = -1;
        return false;
    }
    r.listener = std::thread(acceptLoop, &r);
    return true;
}

void finish(Receiver &r)
{
    r.stop = true;
    if (r.listener.joinable()) r.listener.join();
    if (r.fd >= 0) close(r.fd);
    r.fd = -1;
    while (r.live.load() > 0) usleep(10000);   // each notices the stop within a second
}

/* This TV's IPv4 addresses on the local network: Ethernet and Wi-Fi first,
 * link-local (169.254) ones only when there is nothing else. */
std::vector<std::string> addresses()
{
    std::vector<std::string> wired, other, linkLocal;
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return {};
    for (ifaddrs *i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT))) continue;
        char ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &((sockaddr_in *)i->ifa_addr)->sin_addr, ip, sizeof(ip));
        std::vector<std::string> &into = !strncmp(ip, "169.254.", 8) ? linkLocal
                                       : !strncmp(i->ifa_name, "en", 2) ? wired : other;
        if (std::find(into.begin(), into.end(), ip) == into.end()) into.push_back(ip);
    }
    freeifaddrs(list);
    wired.insert(wired.end(), other.begin(), other.end());
    return wired.empty() ? linkLocal : wired;
}

UIViewController *topController(void)
{
    UIWindow *key = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) {
        if (![sc isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow *w in ((UIWindowScene *)sc).windows) {
            if (w.isKeyWindow || !key) key = w;
        }
    }
    UIViewController *vc = key.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

}   // namespace

/*
 * The page the dialog shows (2026-11-20, as House of the Dead VR's ROM page,
 * src/flat/tvos/main_tvos.m showUpload): the title, how to send the file, a
 * QR code of this TV's address for a phone's camera beside every address it
 * has, and the transfer's progress. A UIAlertController holds text only, so
 * the address was all a player had. Menu, or Cancel, gives up.
 */
@interface GevrUploadController : UIViewController
- (instancetype)initWithTitle:(NSString *)title how:(NSString *)how urls:(NSString *)urls qr:(NSString *)qr;
@property (nonatomic, copy) void (^onCancel)(void);
@property (nonatomic, copy) NSString *status;
@end

static UILabel *gevrUploadLabel(CGFloat size, UIFontWeight weight, UIColor *color, NSString *text)
{
    UILabel *l = [[UILabel alloc] init];
    l.font = [UIFont systemFontOfSize:size weight:weight];
    l.textColor = color;
    l.numberOfLines = 0;
    l.text = text;
    return l;
}

static UIImage *gevrUploadQr(NSString *text)
{
    if (!text.length) return nil;
    CIFilter *f = [CIFilter filterWithName:@"CIQRCodeGenerator"];
    [f setValue:[text dataUsingEncoding:NSUTF8StringEncoding] forKey:@"inputMessage"];
    [f setValue:@"M" forKey:@"inputCorrectionLevel"];
    CIImage *img = [f.outputImage imageByApplyingTransform:CGAffineTransformMakeScale(12, 12)];
    if (!img) return nil;
    CGImageRef cg = [[CIContext contextWithOptions:nil] createCGImage:img fromRect:img.extent];
    if (!cg) return nil;
    UIImage *u = [UIImage imageWithCGImage:cg];
    CGImageRelease(cg);
    return u;
}

@implementation GevrUploadController {
    NSString *_titleText, *_how, *_urls, *_qr;
    UILabel *_statusLabel;
    UIButton *_cancel;
}

- (instancetype)initWithTitle:(NSString *)title how:(NSString *)how urls:(NSString *)urls qr:(NSString *)qr
{
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _titleText = title;
        _how = how;
        _urls = urls;
        _qr = qr;
        self.modalPresentationStyle = UIModalPresentationFullScreen;
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    UIView *v = self.view;
    v.backgroundColor = [UIColor colorWithRed:0.047 green:0.075 blue:0.114 alpha:1];
    UIColor *text = [UIColor colorWithRed:0.93 green:0.95 blue:0.96 alpha:1];
    UIColor *muted = [UIColor colorWithRed:0.6 green:0.65 blue:0.7 alpha:1];
    UIColor *gold = [UIColor colorWithRed:1 green:0.79 blue:0.43 alpha:1];
    UIColor *cyan = [UIColor colorWithRed:0.21 green:0.72 blue:0.81 alpha:1];

    NSMutableArray<UIView *> *row = [NSMutableArray array];
    UIImage *qr = gevrUploadQr(_qr);
    if (qr) {
        // on white, with a margin a phone's camera needs to find the code
        UIView *box = [[UIView alloc] init];
        box.backgroundColor = UIColor.whiteColor;
        box.layer.cornerRadius = 18;
        UIImageView *code = [[UIImageView alloc] initWithImage:qr];
        code.layer.magnificationFilter = kCAFilterNearest;
        code.translatesAutoresizingMaskIntoConstraints = NO;
        [box addSubview:code];
        [NSLayoutConstraint activateConstraints:@[
            [box.widthAnchor constraintEqualToConstant:460],
            [box.heightAnchor constraintEqualToConstant:460],
            [code.centerXAnchor constraintEqualToAnchor:box.centerXAnchor],
            [code.centerYAnchor constraintEqualToAnchor:box.centerYAnchor],
            [code.widthAnchor constraintEqualToConstant:404],
            [code.heightAnchor constraintEqualToConstant:404],
        ]];
        [row addObject:box];
    }

    _statusLabel = gevrUploadLabel(30, UIFontWeightMedium, gold, self.status ?: @"Waiting for the file.");
    _cancel = [UIButton buttonWithType:UIButtonTypeSystem];
    [_cancel setTitle:@"Cancel" forState:UIControlStateNormal];
    [_cancel addTarget:self action:@selector(cancelPressed) forControlEvents:UIControlEventPrimaryActionTriggered];
    NSMutableArray<UIView *> *col = [NSMutableArray arrayWithObjects:
        gevrUploadLabel(54, UIFontWeightBold, gold, _titleText),
        gevrUploadLabel(30, UIFontWeightRegular, text, _how), nil];
    if (_urls.length) [col addObject:gevrUploadLabel(44, UIFontWeightSemibold, cyan, _urls)];
    [col addObjectsFromArray:@[_statusLabel, _cancel,
        gevrUploadLabel(24, UIFontWeightRegular, muted,
                        @"The file goes from your device to this Apple TV only, nowhere else. Menu: cancel.")]];
    UIStackView *column = [[UIStackView alloc] initWithArrangedSubviews:col];
    column.axis = UILayoutConstraintAxisVertical;
    column.alignment = UIStackViewAlignmentLeading;
    column.spacing = 30;
    [row addObject:column];

    UIStackView *page = [[UIStackView alloc] initWithArrangedSubviews:row];
    page.axis = UILayoutConstraintAxisHorizontal;
    page.alignment = UIStackViewAlignmentCenter;
    page.spacing = 90;
    page.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:page];
    UILayoutGuide *g = v.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [page.centerXAnchor constraintEqualToAnchor:g.centerXAnchor],
        [page.centerYAnchor constraintEqualToAnchor:g.centerYAnchor],
        [page.widthAnchor constraintLessThanOrEqualToAnchor:g.widthAnchor multiplier:0.92],
        [column.widthAnchor constraintLessThanOrEqualToConstant:1000],
    ]];
}

- (NSArray<id<UIFocusEnvironment>> *)preferredFocusEnvironments
{
    return _cancel ? @[_cancel] : @[];
}

- (void)setStatus:(NSString *)status
{
    _status = [status copy];
    _statusLabel.text = status;
}

- (void)cancelPressed
{
    if (self.onCancel) self.onCancel();
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    for (UIPress *p in presses) {
        if (p.type == UIPressTypeMenu) {
            [self cancelPressed];
            return;
        }
    }
    [super pressesBegan:presses withEvent:event];
}
@end

/*
 * Shows the dialog and waits. Returns 1 with the received file's path in out
 * (in the app's temporary folder; the caller copies or reads it), 0 when
 * cancelled, -1 when it could not be shown.
 */
extern "C" int gevrIosChooseFile(const char *title, char *out, int outLen)
{
    @autoreleasepool {
        UIViewController *top = topController();
        if (!top) return -1;
        NSFileManager *fm = NSFileManager.defaultManager;
        NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:@"gevr-upload"];
        [fm removeItemAtPath:folder error:nil];   // the previous pick's file
        [fm createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];

        Receiver r;
        r.folder = folder.fileSystemRepresentation;
        r.title = title ? title : "Choose a file";
        if (!start(r)) {
            sysLogPrintf(1, "tvos: no port to receive a file on (errno %d)", errno);
            return -1;
        }
        const std::vector<std::string> where = addresses();
        NSMutableString *urls = [NSMutableString string];
        for (const std::string &ip : where) {
            [urls appendFormat:@"%@http://%s:%d", urls.length ? @"\n" : @"", ip.c_str(), r.port];
        }
        NSString *how = where.empty()
            ? @"This Apple TV is not on a network. Connect it to Wi-Fi or Ethernet, then choose again."
            : @"On a phone or computer on the same network, scan the code with the camera or open the address "
               "in a web browser, and send the file from there.";
        NSString *qr = where.empty() ? nil : [NSString stringWithFormat:@"http://%s:%d", where[0].c_str(), r.port];
        GevrUploadController *alert = [[GevrUploadController alloc] initWithTitle:@(r.title.c_str())
                                                                              how:how urls:urls qr:qr];
        __block bool cancelled = false;
        alert.onCancel = ^{ cancelled = true; };
        // the remote and pads to UIKit while the dialog is up, so Cancel can be pressed
        SDL_SetHintWithPriority(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, "1", SDL_HINT_OVERRIDE);
        [top presentViewController:alert animated:YES completion:nil];
        sysLogPrintf(0, "tvos: waiting for a file on port %d (%d addresses)", r.port, (int)where.size());

        int shown = -1;
        while (!cancelled && r.state.load() == 0) {
            SDL_PumpEvents();
            SDL_Delay(16);
            const long long total = r.total.load(), got = r.got.load();
            const int pct = total > 0 ? (int)(got * 100 / total) : -1;
            if (pct != shown) {
                shown = pct;
                std::string name;
                {
                    std::lock_guard<std::mutex> g(r.lock);
                    name = r.name;
                }
                alert.status = pct < 0 ? @"Waiting for the file."
                                       : [NSString stringWithFormat:@"Receiving %@: %d%%", @(name.c_str()) ?: @"the file", pct];
            }
        }
        const bool received = r.state.load() == 1;
        std::string path;
        {
            std::lock_guard<std::mutex> g(r.lock);
            path = r.path;
        }
        finish(r);
        {   // given up or done: the page goes either way now (Menu no longer dismisses it itself)
            __block bool gone = false;
            [alert dismissViewControllerAnimated:YES completion:^{ gone = true; }];
            for (int i = 0; i < 120 && !gone; i++) {   // the launcher draws again once it is off the screen
                SDL_PumpEvents();
                SDL_Delay(16);
            }
        }
        SDL_SetHintWithPriority(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, "0", SDL_HINT_OVERRIDE);
        if (!received) {
            sysLogPrintf(0, "tvos: file dialog cancelled");
            return 0;
        }
        snprintf(out, outLen, "%s", path.c_str());
        return 1;
    }
}
