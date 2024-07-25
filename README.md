# rlctail

Live stream comments on a Reddit post. This is handy when watching soccer games
or other sporting evets as well as during rapidly developing world events.
The Reddit app used to have a "live" sort mode for comments that basically
did this (sans the ability to delay or replay comments), however in early 2024,
this feature was removed from the app. But it is still possible to implement
this feature using the Reddit API.

```
usage:

  List current match threads:
      rlctail -l <subreddit (sans '/r/' prefix)>

  Tail live comments on post:
      rlctail [-d delaysec] -f <postid>

  Replay comments on post:
      rlctail -t <time> -r <postid>

  For all invocations, alternate credential files can be specified via:
      [-u <usrcredsfile>]
      [-a <appcredsfile>]
```


Setup:

1) Put Reddit user credendials in .rlctail_usercreds.json:

```
{
    "username": "your_username",
    "password": "your_password"

}
```
2) Put app credentials in .rlctail_appcreds.json:
```
{
    "clientid": "app_clientid",
    "clientsecret": "app_clientsecret""

}
```

2) Make sure permissions for .rlctail_usercreds.json and
   .rlctail_appcreds.jsonare are set to 600. The program
   will refuse to run unless both files' permissions are 600
3) Make sure .rlctail_usercreds.json and .rlctail_appcreds.json are listed
   in .gitignore. *** DO NOT STORE CREDENTIALS IN SOURCE CONTROL ***

