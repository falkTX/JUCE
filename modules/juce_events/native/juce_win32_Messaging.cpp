/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

extern HWND juce_messageWindowHandle;

typedef bool (*CheckEventBlockedByModalComps) (const MSG&);
CheckEventBlockedByModalComps isEventBlockedByModalComps = nullptr;

//==============================================================================
namespace WindowsMessageHelpers
{
    const unsigned int specialId             = WM_APP + 0x4400;
    const unsigned int broadcastId           = WM_APP + 0x4403;

    const TCHAR messageWindowName[] = _T("JUCEWindow");
    ScopedPointer<HiddenMessageWindow> messageWindow;

    //==============================================================================
    LRESULT CALLBACK messageWndProc (HWND h, const UINT message, const WPARAM wParam, const LPARAM lParam) noexcept
    {
        JUCE_TRY
        {
            if (h == juce_messageWindowHandle)
            {
                if (message == specialId)
                {
                    // these are trapped early in the dispatch call, but must also be checked
                    // here in case there are windows modal dialog boxes doing their own
                    // dispatch loop and not calling our version

                    Message* const message = reinterpret_cast <Message*> (lParam);
                    MessageManager::getInstance()->deliverMessage (message);
                    message->decReferenceCount();
                    return 0;
                }
                else if (message == broadcastId)
                {
                    const ScopedPointer <String> messageString ((String*) lParam);
                    MessageManager::getInstance()->deliverBroadcastMessage (*messageString);
                    return 0;
                }
                else if (message == WM_COPYDATA && ((const COPYDATASTRUCT*) lParam)->dwData == broadcastId)
                {
                    const COPYDATASTRUCT* data = (COPYDATASTRUCT*) lParam;

                    const String messageString (CharPointer_UTF32 ((const CharPointer_UTF32::CharType*) data->lpData),
                                                data->cbData / sizeof (CharPointer_UTF32::CharType));

                    PostMessage (juce_messageWindowHandle, broadcastId, 0, (LPARAM) new String (messageString));
                    return 0;
                }
            }
        }
        JUCE_CATCH_EXCEPTION

        return DefWindowProc (h, message, wParam, lParam);
    }

    BOOL CALLBACK broadcastEnumWindowProc (HWND hwnd, LPARAM lParam)
    {
        if (hwnd != juce_messageWindowHandle)
            reinterpret_cast <Array<HWND>*> (lParam)->add (hwnd);

        return TRUE;
    }
}

//==============================================================================
bool MessageManager::dispatchNextMessageOnSystemQueue (const bool returnIfNoPendingMessages)
{
    using namespace WindowsMessageHelpers;
    MSG m;

    if (returnIfNoPendingMessages && ! PeekMessage (&m, (HWND) 0, 0, 0, 0))
        return false;

    if (GetMessage (&m, (HWND) 0, 0, 0) >= 0)
    {
        if (m.message == specialId && m.hwnd == juce_messageWindowHandle)
        {
            Message* const message = reinterpret_cast <Message*> (m.lParam);
            MessageManager::getInstance()->deliverMessage (message);
            message->decReferenceCount();
        }
        else if (m.message == WM_QUIT)
        {
            if (JUCEApplicationBase::getInstance() != nullptr)
                JUCEApplicationBase::getInstance()->systemRequestedQuit();
        }
        else if (isEventBlockedByModalComps == nullptr || ! isEventBlockedByModalComps (m))
        {
            if ((m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN)
                 && ! JuceWindowIdentifier::isJUCEWindow (m.hwnd))
            {
                // if it's someone else's window being clicked on, and the focus is
                // currently on a juce window, pass the kb focus over..
                HWND currentFocus = GetFocus();

                if (currentFocus == 0 || JuceWindowIdentifier::isJUCEWindow (currentFocus))
                    SetFocus (m.hwnd);
            }

            TranslateMessage (&m);
            DispatchMessage (&m);
        }
    }

    return true;
}

bool MessageManager::postMessageToSystemQueue (Message* message)
{
    message->incReferenceCount();
    return PostMessage (juce_messageWindowHandle, WindowsMessageHelpers::specialId, 0, (LPARAM) message) != 0;
}

void MessageManager::broadcastMessage (const String& value)
{
    Array<HWND> windows;
    EnumWindows (&WindowsMessageHelpers::broadcastEnumWindowProc, (LPARAM) &windows);

    const String localCopy (value);

    COPYDATASTRUCT data;
    data.dwData = WindowsMessageHelpers::broadcastId;
    data.cbData = (localCopy.length() + 1) * sizeof (CharPointer_UTF32::CharType);
    data.lpData = (void*) localCopy.toUTF32().getAddress();

    for (int i = windows.size(); --i >= 0;)
    {
        HWND hwnd = windows.getUnchecked(i);

        TCHAR windowName [64]; // no need to read longer strings than this
        GetWindowText (hwnd, windowName, 64);
        windowName [63] = 0;

        if (String (windowName) == WindowsMessageHelpers::messageWindowName)
        {
            DWORD_PTR result;
            SendMessageTimeout (hwnd, WM_COPYDATA,
                                (WPARAM) juce_messageWindowHandle,
                                (LPARAM) &data,
                                SMTO_BLOCK | SMTO_ABORTIFHUNG, 8000, &result);
        }
    }
}

//==============================================================================
void MessageManager::doPlatformSpecificInitialisation()
{
    OleInitialize (0);

    using namespace WindowsMessageHelpers;
    messageWindow = new HiddenMessageWindow (messageWindowName, (WNDPROC) messageWndProc);
    juce_messageWindowHandle = messageWindow->getHWND();
}

void MessageManager::doPlatformSpecificShutdown()
{
    WindowsMessageHelpers::messageWindow = nullptr;

    OleUninitialize();
}
