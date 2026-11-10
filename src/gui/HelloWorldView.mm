#include "gui/HelloWorldView.h"

#include "public.sdk/source/common/pluginview.h"

#import <AppKit/AppKit.h>

#include <cstring>

namespace burp {
namespace {

constexpr Steinberg::int32 kWidth = 400;
constexpr Steinberg::int32 kHeight = 200;

class HelloWorldView : public Steinberg::CPluginView {
public:
    HelloWorldView()
    {
        rect = Steinberg::ViewRect(0, 0, kWidth, kHeight);
    }

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE
    {
        return std::strcmp(type, Steinberg::kPlatformTypeNSView) == 0 ? Steinberg::kResultTrue
                                                                      : Steinberg::kResultFalse;
    }

    // systemWindow is the NSView the DAW created for the plugin window.
    void attachedToParent() SMTG_OVERRIDE
    {
        NSView* parent = (__bridge NSView*)systemWindow;

        label_ = [NSTextField labelWithString:@"hello world"];
        label_.font = [NSFont systemFontOfSize:24];
        [label_ sizeToFit];
        const NSSize size = label_.frame.size;
        [label_ setFrameOrigin:NSMakePoint((kWidth - size.width) / 2, (kHeight - size.height) / 2)];
        [parent addSubview:label_];
    }

    void removedFromParent() SMTG_OVERRIDE
    {
        [label_ removeFromSuperview];
        label_ = nil;
    }

private:
    NSTextField* label_ = nil;
};

} // namespace

Steinberg::IPlugView* createHelloWorldView()
{
    return new HelloWorldView;
}

} // namespace burp
