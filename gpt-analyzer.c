#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sys/errno.h>

#define OPENAI_API_URL "https://api.openai.com/v1/chat/completions"
#define MODEL "gpt-4o"
#define API_KEY_FILE "api.key"
#define MAX_MODEL_SIZE 2 << 16
struct MemoryStruct
{
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL)
    {
        free(mem->memory);
        printf("Not enough memory\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

char *read_c_file_without_comments(const char *filename)
{
    FILE *fp;
    long fsize;
    char *input;
    char *output;
    char *p_in; 
    char *p_out;

    enum
    {
        NORMAL,
        SL_COMMENT,
        ML_COMMENT,
        STRING,
        CHAR_CONST
    } state = NORMAL;

    fp = fopen(filename, "r");
    if (!fp)
    {
        perror("fopen");
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        perror("fseek");
        fclose(fp);
        return NULL;
    }
    fsize = ftell(fp);
    if (fsize == -1)
    {
        perror("ftell");
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        perror("rewind");
        fclose(fp);
        return NULL;
    }

    input = malloc(fsize + 1);
    if (!input)
    {
        perror("malloc");
        fclose(fp);
        return NULL;
    }

    output = malloc(fsize + 1);
    if (!output)
    {
        perror("malloc");
        fclose(fp);
        free(input);
        return NULL;
    }

    if (fread(input, 1, fsize, fp) != (size_t)fsize)
    {
        perror("fread");
        fclose(fp);
        free(input);
        free(output);
        return NULL;
    }

    input[fsize] = '\0';
    fclose(fp);

    p_in = input;
    p_out = output;

    while (*p_in)
    {
        if (state == NORMAL)
        {
            if (*p_in == '/' && *(p_in + 1) == '/')
            {
                state = SL_COMMENT;
                p_in += 2;
            }
            else if (*p_in == '/' && *(p_in + 1) == '*')
            {
                state = ML_COMMENT;
                p_in += 2;
            }
            else if (*p_in == '"')
            {
                state = STRING;
                *p_out++ = *p_in++;
            }
            else if (*p_in == '\'')
            {
                state = CHAR_CONST;
                *p_out++ = *p_in++;
            }
            else
            {
                *p_out++ = *p_in++;
            }
        }
        else if (state == SL_COMMENT)
        {
            if (*p_in == '\n')
            {
                state = NORMAL;
                *p_out++ = *p_in++;
            }
            else
            {
                p_in++;
            }
        }
        else if (state == ML_COMMENT)
        {
            if (*p_in == '*' && *(p_in + 1) == '/')
            {
                state = NORMAL;
                p_in += 2;
            }
            else
            {
                p_in++;
            }
        }
        else if (state == STRING)
        {
            if (*p_in == '\\' && *(p_in + 1))
            {
                *p_out++ = *p_in++;
                *p_out++ = *p_in++;
            }
            else if (*p_in == '"')
            {
                state = NORMAL;
                *p_out++ = *p_in++;
            }
            else
            {
                *p_out++ = *p_in++;
            }
        }
        else if (state == CHAR_CONST)
        {
            if (*p_in == '\\' && *(p_in + 1))
            {
                *p_out++ = *p_in++;
                *p_out++ = *p_in++;
            }
            else if (*p_in == '\'')
            {
                state = NORMAL;
                *p_out++ = *p_in++;
            }
            else
            {
                *p_out++ = *p_in++;
            }
        }
    }

    *p_out = '\0';
    free(input);

    return output;
}

int main(int argc, char **argv)
{
    unsigned long source_code_size = 0;
    FILE *fk;
    char *api_key = NULL;
    size_t len = 0;
    ssize_t nread;
    char *source_code;
    const char question[] = "Analyze the C code for bugs, unsafe functions, security issues, POSIX compliance and SEI CERT standard. Provide a detailed report. Here is the code:\n\n";

    cJSON *root;
    cJSON *messages;
    cJSON *message;
    cJSON *response_json;
    cJSON *choices;
    cJSON *first_choice;
    cJSON *message_obj;
    cJSON *content;
    char *json_data;
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    struct MemoryStruct chunk;
    char auth_header[256];
    char *prompt;
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <source_file.c>\n", argv[0]);
        return 1;
    }
    fk = fopen(API_KEY_FILE, "r");
    if (!fk)
    {
        fprintf(stderr, "Error opening api.key file.\n");
        return 1;
    }
    nread = getline(&api_key, &len, fk);
    if (nread == -1)
    {
        free(api_key);
        fprintf(stderr, "Error reading api.key file.\n");
        fclose(fk);
        return 1;
    }
    fclose(fk);

    source_code = read_c_file_without_comments(argv[1]);

    if (!source_code)
    {
        free(api_key);
        fprintf(stderr, "Error reading source file.\n"); 
        return 1;
    }
    source_code_size = strlen(source_code);
    if(source_code_size + strlen(question) + 8  > MAX_MODEL_SIZE)
    {
        free(api_key);
        free(source_code);
        fprintf(stderr, "Source code is too large for the model.\n");
        return 1;
    }
    prompt = malloc(source_code_size + strlen(question) + 8);
    if (!prompt)
    {
        fprintf(stderr, "Memory allocation failed for prompt.\n");
        free(api_key);
        free(source_code);
        return 1;
    }
    snprintf(prompt, source_code_size + strlen(question) + 8, "%s```\n%s\n```", question, source_code);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl)
    {
        free(api_key);
        free(source_code);
        free(prompt);
        fprintf(stderr, "CURL initializaiton failed");
        return 1;
    }
    chunk.memory = malloc(1);
    chunk.size = 0;
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", MODEL);
    messages = cJSON_AddArrayToObject(root, "messages");
    message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", prompt);
    cJSON_AddItemToArray(messages, message);
    json_data = cJSON_PrintUnformatted(root);

    curl_easy_setopt(curl, CURLOPT_URL, OPENAI_API_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        response_json = cJSON_Parse(chunk.memory);
        choices = cJSON_GetObjectItem(response_json, "choices");
        if (choices && cJSON_GetArraySize(choices) > 0)
        {
            first_choice = cJSON_GetArrayItem(choices, 0);
            message_obj = cJSON_GetObjectItem(first_choice, "message");
            content = cJSON_GetObjectItem(message_obj, "content");
            if (content)
            {
                printf("\n=== ChatGPT Analysis ===\n%s\n", content->valuestring);
            }
        }
        else
        {
            printf("Unexpected response from API.\n");
        }

        cJSON_Delete(response_json);
    }
    else
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(prompt);
    free(api_key);
    free(chunk.memory);
    free(source_code);
    free(json_data);
    cJSON_Delete(root);

    curl_global_cleanup();
    return 0;
}
