#!/bin/bash

obj=res
src=replace_synonym.cpp
f=word.vocab.new

g++ -o $obj $src
./$obj $f
