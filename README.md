## A2
A2 is an text editor vim alike and emacs alike, this isn't a project where i want to surpass or be in the same ground as those two, making it for hobby and to learn C better!
<img width="1920" height="1016" alt="image" src="https://github.com/user-attachments/assets/e3daf5d7-c7c5-4f83-b85d-756793cd2e5b" />



## Installation!
Clone the Repository with
```bash
git clone https://github.com/Lucasplaygaemes/a2
```
Then, install the dependencys and run Make with
```bash
./install.sh
```

# Documentation!
Documentation is in - [**Documentation**](./index.md)

# New Release!
I'm being slow making any new releases, i have been really occupied so i don't have a lot of time to spend on here.
But i will keep try my best to fix and add things!

# Important Updates
Before keeping updating the spell or the settings menu or adding the sudo support, i have noticed that, the editor has a line limit! I will try to update it to make it variable.
It will not be a easy to do though, i will need to update every single function that uses the *lines.
It's been really hard making those updates, im truly busy recently, but will keep it up.
I have made some changes and updated with a new thing, a settings file!
Is very simple for now, but i will make it a lot more better in the future! every settings should be customizeable by the settings menu.

# Spell!
Now, with a great effort, a new spell checker was added! What kind of text editor don't have a spell checker?
It's using Hunspell to work! It will be downloaded with the editor, and English will come as default.
There is a command to use it, and it's in the manual, but, to make this and everything a lot more eaiser, i will add...

# Setting Menu!
I will add an menu! to help to set configurations in the a2!
It will contain the things about it, and you will be able to download languages for the spell checker from there!
It will probably contain too the settings panel for the plugins, that we discuss later.
This is a hard to thing to make, so it will be a slow process.
I want to make it the main way to configure a2, and it's shortcuts be changeable and make the most out of customization! It will be a slow, but great thing to add!

# Refactoring
The code has 2 main parts that need to be refactored, the main structure, EditorState and the way that the code handles inputs, isn't the best also, so updates will be made to make it better, but those will take time, and so will delay others things.

# Projects
Now you can save "projects", the windows, workspaces, and files you open, all of them can be open with just a command!
More explanation are in the Documentation!

# Themes!
Now a2 have themes! and they are quite easy to make, Themes will be searched in the default app folder, but, if no found, or if you want to add another folder, you can with the command :set themedir <dir>

# Assembly!
Because i'm learning assembly, a new function was added to the code to help me understand better what C functions turned in what in assembly, i will fix any bugs that i find.
This is a things that i made mostly of fun and learning experience!

# Multiplatform?
a2 isn't compatible with windows, and probably not with mac too.
a2 should be compatible with theoretically every distro whom can run C and ncurse, others things may not work but the core functionality will.

# TODO
Now u can save files, i thought that, running all a2 as root, would be risky, so i made that when trying to save a file, if it fails because of permission, the code would ask if you want to save it as root, then make a copy of the current file, and move it to the designed place!
Even though i could make with another ways, i have chosen this as the most secure, if there's a flaw, i will fix it as soon as possible.
I'm gonna make the shortcuts be user defined too, and saved in a .config file.

# Contribuiting
I would be really grateful if you want to improve the code, it's always open to commits. and if you want you can contact me via my email. My email is lucasplaygaemes@gmail.com, we can chat by email.
