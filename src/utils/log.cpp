#include "xq/utils/log.hpp"
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>


namespace xq {
namespace utils {


// 级别过滤 wrapper
class LevelFilterSink : public spdlog::sinks::sink {
private:
    std::shared_ptr<spdlog::sinks::sink> wrapped_sink_;
    spdlog::level::level_enum target_level_;

public:
    LevelFilterSink(std::shared_ptr<spdlog::sinks::sink> sink, spdlog::level::level_enum level)
        : wrapped_sink_(sink), target_level_(level) {}

    void log(const spdlog::details::log_msg& msg) override {
        if (msg.level == target_level_) {
            wrapped_sink_->log(msg);
        }
    }

    void flush() override {
        wrapped_sink_->flush();
    }

    void set_pattern(const std::string& pattern) override {
        wrapped_sink_->set_pattern(pattern);
    }

    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
        wrapped_sink_->set_formatter(std::move(formatter));
    }
};

} // namespace utils
} // namespace xq

void
xq::utils::init_log(const std::string& log_dir) {
    std::string dir = log_dir;
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    
    // 控制台输出（所有级别）
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::trace);
    
    // 为每个日志级别创建日期分割的文件，并用过滤器只输出对应级别
    auto debug_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(dir + "DEBUG.log", 0, 0);
    auto info_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(dir + "INFO.log", 0, 0);
    auto warn_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(dir + "WARN.log", 0, 0);
    auto error_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(dir + "ERROR.log", 0, 0);
    auto fatal_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(dir + "FATAL.log", 0, 0);
    
    auto debug_filter = std::make_shared<LevelFilterSink>(debug_sink, spdlog::level::debug);
    auto info_filter = std::make_shared<LevelFilterSink>(info_sink, spdlog::level::info);
    auto warn_filter = std::make_shared<LevelFilterSink>(warn_sink, spdlog::level::warn);
    auto error_filter = std::make_shared<LevelFilterSink>(error_sink, spdlog::level::err);
    auto fatal_filter = std::make_shared<LevelFilterSink>(fatal_sink, spdlog::level::critical);
    
    spdlog::sinks_init_list sinks = { 
        console_sink, 
        debug_filter,
        info_filter,
        warn_filter,
        error_filter,
        fatal_filter
    };
    
    auto logger = std::make_shared<spdlog::logger>("xq", sinks);
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
}