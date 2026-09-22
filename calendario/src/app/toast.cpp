#include "app/toast.h"

#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <deque>
#include <format>
#include <string>

#include "core/aumid.h"
#include "core/hr.h"
#include "core/i18n.h"
#include "core/log.h"

namespace agenda {
namespace {

using ABI::Windows::Data::Xml::Dom::IXmlDocument;
using ABI::Windows::Data::Xml::Dom::IXmlDocumentIO;
using ABI::Windows::UI::Notifications::IToastNotification;
using ABI::Windows::UI::Notifications::IToastNotification2;
using ABI::Windows::UI::Notifications::IToastNotificationFactory;
using ABI::Windows::UI::Notifications::IToastNotificationManagerStatics;
using ABI::Windows::UI::Notifications::IToastNotifier;
using ABI::Windows::UI::Notifications::NotificationSetting;
using ABI::Windows::UI::Notifications::NotificationSetting_Enabled;
using ABI::Windows::UI::Notifications::ToastNotification;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

using Activated =
    ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, IInspectable*>;

std::wstring Escape(std::wstring_view text) {
  std::wstring out;
  out.reserve(text.size());
  for (const wchar_t c : text) {
    switch (c) {
      case L'&':
        out += L"&amp;";
        break;
      case L'<':
        out += L"&lt;";
        break;
      case L'>':
        out += L"&gt;";
        break;
      case L'"':
        out += L"&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// The toasts on screen, kept so their Activated handler lives as long as they do. A handful is
// all that is ever up at once; the oldest goes when there are more.
std::deque<ComPtr<IToastNotification>>& Shown() {
  static std::deque<ComPtr<IToastNotification>> shown;
  return shown;
}

}  // namespace

bool ShowReminderToast(const Reminder& reminder, long long now, HWND notify) {
  ComPtr<IToastNotificationManagerStatics> manager;
  if (FAILED(Windows::Foundation::GetActivationFactory(
          HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
          &manager))) {
    return false;
  }
  ComPtr<IToastNotifier> notifier;
  if (FAILED(manager->CreateToastNotifierWithId(HStringReference(kAppUserModelId).Get(),
                                                &notifier))) {
    return false;
  }
  // Without a shortcut declaring the id this fails, and with notifications switched off for
  // Agenda it says so: either way the balloon has to carry it instead.
  NotificationSetting setting{};
  if (FAILED(notifier->get_Setting(&setting)) || setting != NotificationSetting_Enabled) {
    return false;
  }

  // The reminder scenario: it stays until answered and Windows handles snooze and dismiss on its
  // own, with the snooze times offered in a list, so none of that comes back to Agenda.
  const std::wstring xml = std::format(
      LR"(<toast scenario="reminder" launch="agenda">)"
      LR"(<visual><binding template="ToastGeneric"><text>{}</text><text>{}</text>)"
      LR"(<text placement="attribution">{}</text></binding></visual>)"
      LR"(<actions><input id="snooze" type="selection" defaultInput="5">)"
      LR"(<selection id="5" content="{}"/><selection id="10" content="{}"/>)"
      LR"(<selection id="30" content="{}"/></input>)"
      LR"(<action activationType="system" arguments="snooze" hint-inputId="snooze" content="{}"/>)"
      LR"(<action activationType="system" arguments="dismiss" content="{}"/></actions>)"
      LR"(<audio src="ms-winsoundevent:Notification.Reminder"/></toast>)",
      Escape(reminder.title), Escape(ReminderWhen(reminder, now)),
      Escape(ReminderSoon(reminder, now)), T(L"5 minutos", L"5 minutes"),
      T(L"10 minutos", L"10 minutes"), T(L"30 minutos", L"30 minutes"),
      T(L"Posponer", L"Snooze"), T(L"Descartar", L"Dismiss"));

  ComPtr<IInspectable> inspectable;
  if (FAILED(RoActivateInstance(
          HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(), &inspectable))) {
    return false;
  }
  ComPtr<IXmlDocument> document;
  ComPtr<IXmlDocumentIO> io;
  if (FAILED(inspectable.As(&document)) || FAILED(document.As(&io)) ||
      Failed(io->LoadXml(HStringReference(xml.c_str()).Get()), L"toast: LoadXml")) {
    return false;
  }

  ComPtr<IToastNotificationFactory> factory;
  ComPtr<IToastNotification> toast;
  if (FAILED(Windows::Foundation::GetActivationFactory(
          HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
          &factory)) ||
      FAILED(factory->CreateToastNotification(document.Get(), &toast))) {
    return false;
  }

  // One per occurrence and lead, so a second pass over the same minute replaces instead of
  // stacking, and a snoozed one is not doubled by the next check.
  ComPtr<IToastNotification2> tagged;
  if (SUCCEEDED(toast.As(&tagged))) {
    const std::wstring tag = std::format(L"{}-{}", reminder.at, reminder.uid.substr(0, 40));
    tagged->put_Tag(HStringReference(tag.c_str()).Get());
    tagged->put_Group(HStringReference(L"reminders").Get());
  }

  // A click on the body while Agenda runs opens the popup on that day. It arrives on a thread of
  // the notification platform, so all it does is post.
  const WPARAM day =
      static_cast<WPARAM>(std::chrono::sys_days{reminder.day}.time_since_epoch().count());
  EventRegistrationToken token{};
  toast->add_Activated(
      Callback<Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Activated, Microsoft::WRL::FtmBase>>(
          [notify, day](IToastNotification*, IInspectable*) -> HRESULT {
            PostMessageW(notify, kReminderOpenMessage, day, 0);
            return S_OK;
          })
          .Get(),
      &token);

  if (Failed(notifier->Show(toast.Get()), L"toast: IToastNotifier::Show")) return false;
  Shown().push_back(toast);
  if (Shown().size() > 16) Shown().pop_front();
  return true;
}

}  // namespace agenda
