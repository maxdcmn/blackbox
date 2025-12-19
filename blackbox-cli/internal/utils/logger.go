package utils

import (
	"fmt"
	"os"
	"time"
)

var debugEnabled = false
var logFile *os.File

func InitLogger(debug bool, logPath string) error {
	debugEnabled = debug
	if logPath != "" {
		var err error
		logFile, err = os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			return err
		}
	}
	return nil
}

func CloseLogger() {
	if logFile != nil {
		logFile.Close()
	}
}

func log(level, msg string, args ...interface{}) {
	if !debugEnabled && level == "DEBUG" {
		return
	}
	
	timestamp := time.Now().Format("2006-01-02 15:04:05.000")
	formatted := fmt.Sprintf(msg, args...)
	line := fmt.Sprintf("[%s] %s: %s\n", timestamp, level, formatted)
	
	if logFile != nil {
		logFile.WriteString(line)
	} else {
		fmt.Fprintf(os.Stderr, line)
	}
}

func Debug(msg string, args ...interface{}) {
	log("DEBUG", msg, args...)
}

func Info(msg string, args ...interface{}) {
	log("INFO", msg, args...)
}

func Warn(msg string, args ...interface{}) {
	log("WARN", msg, args...)
}

func Error(msg string, args ...interface{}) {
	log("ERROR", msg, args...)
}


