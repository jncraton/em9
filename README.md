# em9

A simple text editor. The name "nano" was already taken, so I went with em9 (E to the minus 9).

This editor is a fork of the excellent, light-weight [Sanos editor](http://www.jbox.dk/sanos/editor.htm). I've tried to stick with the following principles from that editor:

> While most other text-mode editors (e.g. vi, emacs, and nano) have custom key bindings, the Sanos text editor tries to follow the principle of least surprise by using the same key bindings as most GUI-based text editors like gedit and notepad. These key bindings should also be familiar to anyone who has used a web browsers. You navigate around in the text using the normal cursor control keys (up, down, left, right, home, end, pgup, pgdn) and text can be selected by holding down the shift key while navigating. The text editor has a clipboard and you can cut (Ctrl+X), copy (Ctrl+C), and paste (Ctrl+V) text. Other commands also use the standard key bindings for GUI applications, like Ctrl+O to open a file, Ctrl+S to save the file, and Ctrl+Q to quit the editor.

As the Sanos editor is written for the Sanos operating system, it lacks much integration with Linux or other mainstream operating systems. For example, it uses an internal clipboard buffer rather than integrating with the OS clipboard. One of my goals for this fork is to have tigher integration with Linux and greater flexibility when invoking the editor from the command line. The original Sanos editor accepts no flags or configuration options.

I have made the following changes:

- Ctrl+d to duplicate the current selection or line
- Cut/Copy now operate on the current line if there is no active selection
- Movement Commands - These can be accessed via Ctrl+g or appended to filenames when opening
    - `:{line number}` - Jumps to a `line number`
    - `#{needle}` - Jumps to next occurrence of `needle` in file
