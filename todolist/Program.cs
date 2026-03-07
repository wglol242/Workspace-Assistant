using System;
using System.Net.Http;
using System.Threading.Tasks;
using Windows.Media.Control;
using Windows.UI.Shell;

class Program
{
    static async Task Main(string[] args)
    {
        Console.WriteLine("ESP32 Notifier Started... (Press 'Q' to quit)");

        var notifier = new EspNotifier();
        await notifier.InitializeAsync();

        while (Console.ReadKey(true).Key != ConsoleKey.Q) { }
    }
}

public class EspNotifier
{
    private readonly string espBaseUrl = "";
    private FocusSessionManager focusManager;
    private GlobalSystemMediaTransportControlsSessionManager mediaManager;
    private static readonly HttpClient client = new HttpClient();

    public async Task InitializeAsync()
    {
        if (FocusSessionManager.IsSupported)
        {
            focusManager = FocusSessionManager.GetDefault();
            focusManager.IsFocusActiveChanged += (s, e) => SendFocusState(s.IsFocusActive);
            SendFocusState(focusManager.IsFocusActive);
        }

        try
        {
            mediaManager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            mediaManager.CurrentSessionChanged += MediaManager_CurrentSessionChanged;
            CheckCurrentSession();
        }
        catch (Exception ex)
        {
        }
    }

    private void SendFocusState(bool isActive)
    {
        string state = isActive ? "on" : "off";
        string url = $"{espBaseUrl}/focus?status={state}";
        SendToEsp(url, "");
    }

    private void MediaManager_CurrentSessionChanged(GlobalSystemMediaTransportControlsSessionManager sender, CurrentSessionChangedEventArgs args)
    {
        CheckCurrentSession();
    }

    private void CheckCurrentSession()
    {
        var session = mediaManager.GetCurrentSession();
        if (session != null)
        {
            session.MediaPropertiesChanged -= Session_MediaPropertiesChanged;
            session.MediaPropertiesChanged += Session_MediaPropertiesChanged;
            session.PlaybackInfoChanged -= Session_PlaybackInfoChanged;
            session.PlaybackInfoChanged += Session_PlaybackInfoChanged;

            _ = UpdateMediaInfo(session);
        }
        else
        {
            SendToEsp($"{espBaseUrl}/music?title=Stopped&artist=", "");
        }
    }

    private void Session_PlaybackInfoChanged(GlobalSystemMediaTransportControlsSession session, PlaybackInfoChangedEventArgs args)
    {
        _ = UpdateMediaInfo(session);
    }

    private void Session_MediaPropertiesChanged(GlobalSystemMediaTransportControlsSession session, MediaPropertiesChangedEventArgs args)
    {
        _ = UpdateMediaInfo(session);
    }

    private async Task UpdateMediaInfo(GlobalSystemMediaTransportControlsSession session)
    {
        try
        {
            var info = session.GetPlaybackInfo();
            if (info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
            {
                var props = await session.TryGetMediaPropertiesAsync();

                string title = Uri.EscapeDataString(props.Title);
                string artist = Uri.EscapeDataString(props.Artist);

                string url = $"{espBaseUrl}/music?title={title}&artist={artist}";
                SendToEsp(url, "");
            }
            else if (info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused)
            {
                SendToEsp($"{espBaseUrl}/music?title=Paused&artist=", "");
            }
        }
        catch (Exception ex)
        {
        }
    }

    private async void SendToEsp(string url, string logMsg)
    {
        try
        {
            await client.GetAsync(url);
        }
        catch
        {
        }
    }
}
