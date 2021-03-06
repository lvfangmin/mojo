/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sky/engine/config.h"
#include "sky/engine/web/ChromeClientImpl.h"

#include "base/logging.h"
#include "gen/sky/platform/RuntimeEnabledFeatures.h"
#include "sky/engine/bindings/core/v8/ScriptController.h"
#include "sky/engine/core/dom/Document.h"
#include "sky/engine/core/dom/Element.h"
#include "sky/engine/core/dom/Node.h"
#include "sky/engine/core/events/KeyboardEvent.h"
#include "sky/engine/core/frame/Console.h"
#include "sky/engine/core/frame/FrameView.h"
#include "sky/engine/core/frame/Settings.h"
#include "sky/engine/core/page/Page.h"
#include "sky/engine/core/rendering/HitTestResult.h"
#include "sky/engine/platform/Cursor.h"
#include "sky/engine/platform/NotImplemented.h"
#include "sky/engine/platform/PlatformScreen.h"
#include "sky/engine/platform/exported/WrappedResourceRequest.h"
#include "sky/engine/platform/geometry/FloatRect.h"
#include "sky/engine/platform/geometry/IntRect.h"
#include "sky/engine/public/platform/Platform.h"
#include "sky/engine/public/platform/WebCursorInfo.h"
#include "sky/engine/public/platform/WebInputEvent.h"
#include "sky/engine/public/platform/WebRect.h"
#include "sky/engine/public/platform/WebURLRequest.h"
#include "sky/engine/public/web/Sky.h"
#include "sky/engine/public/web/WebConsoleMessage.h"
#include "sky/engine/public/web/WebFrameClient.h"
#include "sky/engine/public/web/WebNode.h"
#include "sky/engine/public/web/WebSettings.h"
#include "sky/engine/public/web/WebTextDirection.h"
#include "sky/engine/public/web/WebViewClient.h"
#include "sky/engine/web/WebLocalFrameImpl.h"
#include "sky/engine/web/WebSettingsImpl.h"
#include "sky/engine/web/WebViewImpl.h"
#include "sky/engine/wtf/text/CString.h"
#include "sky/engine/wtf/text/StringBuilder.h"
#include "sky/engine/wtf/text/StringConcatenate.h"
#include "sky/engine/wtf/unicode/CharacterNames.h"

namespace blink {

ChromeClientImpl::ChromeClientImpl(WebViewImpl* webView)
    : m_webView(webView)
{
}

ChromeClientImpl::~ChromeClientImpl()
{
}

void* ChromeClientImpl::webView() const
{
    return static_cast<void*>(m_webView);
}

void ChromeClientImpl::setWindowRect(const FloatRect& r)
{
    if (m_webView->client())
        m_webView->client()->setWindowRect(IntRect(r));
}

FloatRect ChromeClientImpl::windowRect()
{
    WebRect rect;
    if (m_webView->client())
        rect = m_webView->client()->rootWindowRect();
    else {
        // These numbers will be fairly wrong. The window's x/y coordinates will
        // be the top left corner of the screen and the size will be the content
        // size instead of the window size.
        rect.width = m_webView->size().width;
        rect.height = m_webView->size().height;
    }
    return FloatRect(rect);
}

void ChromeClientImpl::focus()
{
}

bool ChromeClientImpl::canTakeFocus(FocusType)
{
    // For now the browser can always take focus if we're not running layout
    // tests.
    return !layoutTestMode();
}

void ChromeClientImpl::takeFocus(FocusType type)
{
    if (!m_webView->client())
        return;
    if (type == FocusTypeBackward)
        m_webView->client()->focusPrevious();
    else
        m_webView->client()->focusNext();
}

void ChromeClientImpl::focusedNodeChanged(Node* node)
{
    m_webView->client()->focusedNodeChanged(WebNode(node));

    WebURL focusURL;
    if (node && node->isElementNode() && toElement(node)->isLiveLink())
        focusURL = toElement(node)->hrefURL();
    m_webView->client()->setKeyboardFocusURL(focusURL);
}

void ChromeClientImpl::focusedFrameChanged(LocalFrame* frame)
{
    WebLocalFrameImpl* webframe = WebLocalFrameImpl::fromFrame(frame);
    if (webframe && webframe->client())
        webframe->client()->frameFocused();
}

WebNavigationPolicy ChromeClientImpl::getNavigationPolicy()
{
    return WebNavigationPolicyCurrentTab;
}

bool ChromeClientImpl::shouldReportDetailedMessageForSource(const String& url)
{
    WebLocalFrameImpl* webframe = m_webView->mainFrameImpl();
    return webframe->client() && webframe->client()->shouldReportDetailedMessageForSource(url);
}

inline static String messageLevelAsString(MessageLevel level)
{
    switch(level) {
        case DebugMessageLevel:
            return "DEBUG";
        case LogMessageLevel:
            return "LOG";
        case WarningMessageLevel:
            return "WARNING";
        case ErrorMessageLevel:
            return "ERROR";
        case InfoMessageLevel:
            return "INFO";
    }
    return "MESSAGE:";
}


void ChromeClientImpl::addMessageToConsole(LocalFrame* localFrame, MessageSource source, MessageLevel level, const String& message, unsigned lineNumber, const String& sourceID, const String& stackTrace)
{

    if (level == ErrorMessageLevel) {
        printf("ERROR: %s \nSOURCE: %s:%u\n", message.utf8().data(), sourceID.utf8().data(), lineNumber);
    } else {
#if OS(ANDROID)
        LOG(INFO) << "CONSOLE: " << messageLevelAsString(level).utf8().data()
            << ": " << message.utf8().data();
#else
        printf("CONSOLE: %s: %s\n", messageLevelAsString(level).utf8().data(), message.utf8().data());
#endif
    }
    fflush(stdout);

    WebLocalFrameImpl* frame = WebLocalFrameImpl::fromFrame(localFrame);
    if (frame && frame->client()) {
        frame->client()->didAddMessageToConsole(
            WebConsoleMessage(static_cast<WebConsoleMessage::Level>(level), message),
            sourceID,
            lineNumber,
            stackTrace);
    }
}

void ChromeClientImpl::scheduleVisualUpdate()
{
    m_webView->scheduleVisualUpdate();
}

IntRect ChromeClientImpl::rootViewToScreen(const IntRect& rect) const
{
    IntRect screenRect(rect);

    if (m_webView->client()) {
        WebRect windowRect = m_webView->client()->windowRect();
        screenRect.move(windowRect.x, windowRect.y);
    }

    return screenRect;
}

WebScreenInfo ChromeClientImpl::screenInfo() const
{
    return m_webView->client() ? m_webView->client()->screenInfo() : WebScreenInfo();
}

void ChromeClientImpl::setCursor(const Cursor& cursor)
{
    setCursor(WebCursorInfo(cursor));
}

void ChromeClientImpl::setCursor(const WebCursorInfo& cursor)
{
}

String ChromeClientImpl::acceptLanguages()
{
    return m_webView->client()->acceptLanguages();
}

} // namespace blink
