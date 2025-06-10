C Source Code Analyzer with ChatGPT API

This program reads a C source file, strips out comments, and submits the resulting code to the OpenAI ChatGPT API for analysis.

The AI is asked to analyze the C code for:

Bugs
Unsafe functions
Security issues
POSIX compliance
MISRA C:2023 compliance

The program uses:

libcurl for HTTP requests
cJSON for JSON encoding
The OpenAI Chat Completions API

Features

. Strips single-line (//) and multi-line (/* */) comments from the source file
. Submits the "cleaned" code to the ChatGPT API
. Automatically formats the code in a markdown code block
. Prints the AI's analysis in the terminal

Requirements

GCC or any C99-compliant compiler
libcurl development package
cJSON library
An OpenAI API key
1. Get an OpenAI API key
Go to platform.openai.com and generate an API key.
Save it in a file called:
api.key

The file should contain only the API key on one line.

2. Build the program
gcc -o gpt-analyzer gpt-analyzer.c -lcurl -lcjson

3. Run the analyzer
./gpt-analyze source_code.c
Where source_code.c is the C file you want to analyze.

Notes

The source code is stripped of comments before being sent to the AI. This ensures that the analysis focuses on the actual C code.
The model used is gpt-4o. You can modify the model field in the source to use a different model.
The maximum size for the prompt is defined by MAX_MODEL_SIZE. Large files will be rejected if they exceed this size.
