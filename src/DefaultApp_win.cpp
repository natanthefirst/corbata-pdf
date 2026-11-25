#include "DefaultApp.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QUrl>

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

namespace {

const wchar_t kProgId[] = L"CorbataPDF.Document";
const wchar_t kAppName[] = L"Corbata PDF";
const wchar_t kCapabilities[] = L"Software\\Corbata\\Corbata PDF\\Capabilities";

std::wstring exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
}

void setString(const std::wstring &key, const wchar_t *name, const std::wstring &value)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                    DWORD((value.size() + 1) * sizeof(wchar_t)));
}

std::wstring getString(const std::wstring &key, const wchar_t *name)
{
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    return buf;
}

} // namespace

namespace DefaultApp {

bool isSupported() { return true; }

bool isDefault()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD len = DWORD(std::size(buf));
    if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L".pdf", L"open", buf, &len)))
        return false;
    return QString::fromWCharArray(buf).compare(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
                                                Qt::CaseInsensitive) == 0;
}

void registerApp()
{
    const std::wstring exe = exePath();
    const std::wstring command = L"\"" + exe + L"\" \"%1\"";
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring progId = classes + kProgId;
    if (getString(progId + L"\\shell\\open\\command", nullptr) == command)
        return; // Already registered at this location.

    // The document type ("ProgID") that opens PDFs with this exe.
    setString(progId, nullptr, L"PDF Document");
    setString(progId + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    setString(progId + L"\\shell\\open\\command", nullptr, command);
    // Offer it for .pdf in "Open with".
    setString(classes + L".pdf\\OpenWithProgids", kProgId, L"");
    const std::wstring app = classes + L"Applications\\Corbata PDF.exe";
    setString(app, L"FriendlyAppName", kAppName);
    setString(app + L"\\SupportedTypes", L".pdf", L"");
    setString(app + L"\\shell\\open\\command", nullptr, command);
    // List it in Settings ▸ Apps ▸ Default apps.
    setString(kCapabilities, L"ApplicationName", kAppName);
    setString(kCapabilities, L"ApplicationDescription", L"Rearrange, merge, split and rotate PDF pages.");
    setString(kCapabilities, L"ApplicationIcon", L"\"" + exe + L"\",0");
    setString(std::wstring(kCapabilities) + L"\\FileAssociations", L".pdf", kProgId);
    setString(L"Software\\RegisteredApplications", kAppName, kCapabilities);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void makeDefault(QWidget *parent)
{
    registerApp();
    if (isDefault()) {
        QMessageBox::information(parent, QObject::tr("Default PDF App"),
                                 QObject::tr("Corbata PDF is already your default app for PDF files."));
        return;
    }
    QMessageBox::information(
        parent, QObject::tr("Default PDF App"),
        QObject::tr("Windows only lets you choose the default app yourself.\n\n"
                    "Settings will open on Corbata PDF's page: click \"Set default\" "
                    "(or click the box next to .pdf and pick Corbata PDF)."));
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=Corbata PDF")));
}

} // namespace DefaultApp
