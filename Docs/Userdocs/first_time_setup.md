# First-Time Setup

If you have a previous SM5 installation using GrooveStats Launcher, follow the [StepMania 5.1 to ITGmania Migration Guide](./sm5_migration.md).

Otherwise, all you need to do is configure `GrooveStats.ini` in your profile folder as originally documented for the [GrooveStats Launcher](https://github.com/GrooveStats/gslauncher/blob/main/README.md). This can be done in one of two ways:

1. Enter the Music Select screen when logged into a profile. This will automatically generate the file for you.
2. Identify your LocalProfile, and manually create a GrooveStats.ini with the following contents:
   ```
   [GrooveStats]
   ApiKey=YOUR_API_KEY
   IsPadPlayer=1
   ```
   `IsPadPlayer=1` indicates that you are playing on a pad (and not a keyboard). If you're using a keyboard, then set this to 0. In that case your scores will not be submitted to GrooveStats, but other functionality will be active.

To obtain your API key to make requests to the GrooveStats service, log into your GrooveStats account and visit the [Update Profile](https://groovestats.com/index.php?page=register&action=update) page to generate and copy your API key. Paste the API key after the `ApiKey=` in the `GrooveStats.ini` file.

![GS API Key](./images/gs-api-key.png)
