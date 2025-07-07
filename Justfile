default:
    @just --list

run:
    idf.py build && idf.py flash && idf.py monitor
