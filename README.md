# mini-generational-gc

## Description

this is a simple generational garbage collector which is implemented based on mini-gc, 
referenced from https://github.com/authorNari/minigc .

At present, this program can only run on the x86 architecture, because the inline assembly instruction is used when running the test function.

## Algorithms

mark & sweep

coping gc

## Usage
use "test" as a parameter of the main function to run execute "test" function.

$ make all

$ ./gc test
