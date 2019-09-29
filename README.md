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

Don't use em9. I'd recommend `sed -i s/needle/replacement/`

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

Simple Design
=============

em9 focuses on keeping its design very simple. It tries to focus on modelling what you see on the screen with its internal datastructures rather than being overly concerned with runtime performance.

Core Datastructures
-------------------

The editor is built around a single buffer represented as a list of soft-wrapped screen lines. em9 always has wrapping enabled. That decision will no doubt disappoint some folks, but it simplifies the internals in several ways:

- em9 does not need to support horizontal scrolling under any circumstances
- There are no calculations needed to determine what can fit in the terminal. We already have our file serialized as terminal lines.
- lines can be represented internally as a fixed-size datastructure without needing special cases to handle files with excessively long lines
- Movement is simple. Moving down increments the current line. Moving right increments the current column.

As far as I know, no other editors operate this way and instead use gap buffers, ropes, or other more complicated data structures internally. em9's design does carry some significant drawbacks:

- A single character edit requires reallocating memory for the entire file contents. This may seem like a deal breaker, but for small text files, this really doesn't take long, although I'll admit that it feels gross. Others may choose to load an entire browser engine in order to edit 5 lines of text. If we're able to sacrifice a little runtime efficiency, I'd prefer a very lean and straightforward design.

Memory
------

em9 won't leak memory.

This may seem like an outrageous claim for a piece of software written in C. Often folks rewrite applications in Rust or Go in order to make claims like this, but I feel confident in the memory safety of em9. Here's why:

em9 does not use any dynamic memory allocations.

It statically allocates enough storage space for most reasonable text files. Larger files are not able to be edited. This means it uses over a megabyte of RAM editing a 1 line file, but it won't ever use more.

This decision simplifies the design and also reduces the number of external dependencies.
