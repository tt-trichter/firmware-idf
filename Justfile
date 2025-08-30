default:
    @just --list

run:
    idf.py build && idf.py flash --port /dev/ttyACM0 && idf.py monitor --port /dev/ttyACM0

build: 
    idf.py build

config:
    idf.py menuconfig --style monochrome
