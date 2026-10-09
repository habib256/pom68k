cmake_minimum_required(VERSION 3.5)
project(POM68KNetProbe C)
add_application(NetProbe main.c)
set_property(TARGET NetProbe PROPERTY C_STANDARD 11)
