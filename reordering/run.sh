#!/bin/bash

# Exit immediately if any compilation command fails
set -e

echo "Compiling rv64i_gen..."
g++ -std=c++17 -o rv64i_gen rv64i.cpp

echo "Compiling tester..."
g++ -std=c++17 -o tester tester.cpp parser.cpp DAG.cpp scheduler.cpp heurestic.cpp

echo "Compilation successful. Starting execution loop (1000 iterations)..."

# Run the loop exactly 1000 times
for ((i=1; i<=1000; i++)); do
    echo "----------------------------------------"
    echo "Iteration $i of 1000"
    echo "----------------------------------------"

    # Run the generator
    ./rv64i_gen

    # Run the tester with the generated instructions
    ./tester rv64i_instructions.txt

    # Check if the tester exited with a failure code (non-zero)
    if [ $? -ne 0 ]; then
        echo "Failure detected on iteration $i! Stopping loop."
        exit 1
    fi
done

echo "Success! All 1000 iterations completed without errors."
