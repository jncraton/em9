em9
===

[![Build Status](https://travis-ci.org/jncraton/em9.svg?branch=master)](https://travis-ci.org/jncraton/em9)

A small, simple text editor using modern GUI keybindings. The names "[pico](https://en.wikipedia.org/wiki/Pico_(text_editor))", "[micro](https://micro-editor.github.io/)", and "[nano](https://en.wikipedia.org/wiki/GNU_nano)" were already taken, so I went with em9 (E to the minus 9).

My goal is for em9 to be able edit a text file effectively and do nothing else. It will not:

- Function as an IDE in any way

- Be capable of running shell commands

- Be able to edit multiple files at once

- Support plugins. The source code is short and simple, so you can add any features that matter to you. If a feature is small and you believe it would be useful to most users, feel free to send a pull request.

How Do I...
===========

em9 is designed to force you to use native operating system and shell features in place of embedding these features in the editor itself. Here are some example operations.

Find and replace in a file
--------------------------

Don't use em9. I'd recommend `sed -i s/needle/replacement/g {filename}`

Append content from a shell command
-----------------------------------

Don't use em9. Use `command >> file`.

Find a file in a project
------------------------

Don't use em9. It is a text editor, not an IDE or project management tool. You could perhaps use:

- `find . -iname {filename}`

- `git grep {text}`

- `fd {filename}`

Insert content from a shell command
-----------------------------------

1. Use `command | xsel` to get the output in the clipboard.

2. Open the file with em9 and paste away.

Open a file to a specific line
------------------------------

Invoke as `em9 [filename] :linenumber` e.g. `em9 README.md :10`.
