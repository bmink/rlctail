# rlctail

CLI "live" display for Reddit comments on a post. I mostly use this when
streaming soccer games to display the match thread comments. Reddit's
official app has a live mode which works OK (for the most part), but there is
a big problem: the fact that everybody is watching different streams in
different parts of the world, yet is commenting on the same post. This means
that unless your stream is amongst the fastest ones, you will learn about
big moments in the game from the Reddit comments seconds before you actually
see them on the screen. Enter rlctail, which can delay the comments thread
display by a specified number of seconds.


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

