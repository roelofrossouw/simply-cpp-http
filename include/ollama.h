#ifndef SC_OLLAMA_H
#define SC_OLLAMA_H
#include <string>
#include <map>
#include <vector>

namespace sc {
    // A client for an Ollama server's generate API, by default at http://127.0.0.1:11434. Without
    // a model it takes the server's first one, so making one asks the server.
    class ollama {
        static constexpr std::string_view DEFAULT_IP = "http://127.0.0.1";
        static constexpr int DEFAULT_PORT = 11434;
        static constexpr int DEFAULT_MAX_TOKENS = 512;

    public:
        ollama(const std::string &model = {}, const std::string &host = std::string(DEFAULT_IP), int port = DEFAULT_PORT);

        std::vector<std::string> models();

        // The model's response to prompt. images are base64 data, or (under 1000 characters) file
        // names to read. The context it returns is kept for the next call, as a conversation.
        std::string generate(const std::string &prompt, const std::vector<std::string> &images = {});

        // The last result's statistics (durations, token counts) as JSON text.
        std::string stats();

        int context_size() const { return context.size(); }

        std::string getLastResult() const { return last_result; }
        std::string getUrl() const { return url; }
        std::string getModel() const { return model; }
        std::string getFormat() const { return format; }
        std::string getKeepAlive() const { return keep_alive; }
        int getMaxTokens() const { return max_tokens; }
        bool isStream() const { return stream; }
        float getTemperature() const { return temperature; }
        bool isThink() const { return think; }
        std::vector<int> getContext() { return context; }

        void setUrl(const std::string &url) { this->url = url; }
        void setModel(const std::string &model) { this->model = model; }
        void clearFormat() { this->format = ""; }
        void setKeepAlive(std::string keep_alive) { this->keep_alive = keep_alive; }
        void setMaxTokens(int max_tokens) { this->max_tokens = max_tokens; }
        void setTopP(double top_p) { this->top_p = top_p; }
        void setTopK(int top_k) { this->top_k = top_k; }
        void setRepeatPenalty(double repeat_penalty) { this->repeat_penalty = repeat_penalty; }
        void setNumPredict(double num_predict) { this->num_predict = num_predict; }
        void setNumCtx(double num_ctx) { this->num_ctx = num_ctx; }
        void setSeed(double seed) { this->seed = seed; }
        void setStream(bool stream) { this->stream = stream; }
        void setTemperature(float temperature) { this->temperature = temperature; }
        void setThink(bool think) { this->think = think; }
        void setContext(std::vector<int> context) { this->context = context; }
        void setTimeout(int timeout) { this->timeout = timeout; }

        // "json", or a JSON schema (as text) for the response to follow; surrounding quotes are
        // dropped.
        void setFormat(std::string format_string);

        // One request as JSON in, the result as JSON text out: model, server, port, instruction (the
        // text, or a file of it), and optionally data (added to the instruction), images, schema
        // and the generation options. The result has the model's response (parsed when it's JSON)
        // and ai_data statistics; a failure is reported in "error", not thrown. Debug builds use
        // debug_model, debug_server and debug_port when the request has them.
        static std::string process(const std::string &json_request);

    private:
        std::string url;
        std::string model;
        std::string format{"json"};
        std::string keep_alive{"30m"};
        int max_tokens{DEFAULT_MAX_TOKENS};
        float top_p{1.0};
        int top_k{1};
        float repeat_penalty{1.0};
        int num_predict{4096};
        int num_ctx{8192};
        int seed{180001};
        int timeout{60};
        bool stream{false};
        float temperature{0.5};
        bool think{false};

        std::string last_result{};
        std::vector<int> context{};

        static void remove_backtick(std::string &input, const std::string &marker, bool mid_string);
    };
}

#endif //SC_OLLAMA_H
