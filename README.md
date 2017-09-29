# em9 ![build status](https://api.travis-ci.org/jncraton/em9.png)

A simple text editor. The name "nano" was already taken, so I went with em9 (E to the minus 9).

This editor is a fork of the excellent, light-weight [Sanos editor](http://www.jbox.dk/sanos/editor.htm). I've tried to stick with the following principles from that editor:

> While most other text-mode editors (e.g. vi, emacs, and nano) have custom key bindings, the Sanos text editor tries to follow the principle of least surprise by using the same key bindings as most GUI-based text editors like gedit and notepad. These key bindings should also be familiar to anyone who has used a web browsers. You navigate around in the text using the normal cursor control keys (up, down, left, right, home, end, pgup, pgdn) and text can be selected by holding down the shift key while navigating. The text editor has a clipboard and you can cut (Ctrl+X), copy (Ctrl+C), and paste (Ctrl+V) text. Other commands also use the standard key bindings for GUI applications, like Ctrl+O to open a file, Ctrl+S to save the file, and Ctrl+Q to quit the editor.

As the Sanos editor is written for the Sanos operating system, it lacks much integration with Linux or other mainstream operating systems. For example, it uses an internal clipboard buffer rather than integrating with the OS clipboard. One of my goals for this fork is to have tigher integration with Linux and greater flexibility when invoking the editor from the command line. The original Sanos editor accepts no flags or configuration options.

My goal is for em9 to be able edit a text file effectively. Sticking with the single responsibility principle, em9 will:

- Not functioning as an IDE in any way
- Not be capable of running shell commands
- Only be able to edit one file at a time
- Not support plugins. The source code is short and simple, so you can add any features that matter to you. If a feature is small and you believe it would be useful to most users, feel free to send a pull request.

# How Do I...

em9 is designed to force you to use native operating system and shell features in place of embedding these features in the editor itself. Here are some example operations.

## Find and replace in a file

Don't use em9. I'd recommend `sed -i s/needle/replacement/`

## Appending content from a shell command

Don't use em9 at all. Use `command >> file`.

## Inserting content from a shell command

1. Use `command | xsel` to get the output in the clipboard.
2. Open the file with em9 and paste away.

## Finding a file in a project

Don't use em9 at all. It is a text editor, not an IDE or project management tool. You could use:

- `find . -iname {filename}`
- `git grep {text}` nding a file in a project

Don't use em9 at all. It is a text editor, not an IDE or project management tool. You could use:

- `find . -iname {filename}`
- `git grep {text}` nding a file in a project

Don't use em9 at all. It is a text editor, not an IDE or project management tool. You could use:

- `find . -iname {filename}`
- `git grep {text}` 