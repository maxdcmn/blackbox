.PHONY: all build clean install help

# Default target - build the server
all: build

# Build blackbox-server
build:
	@echo "Building blackbox-server..."
	@cd blackbox-server && \
		mkdir -p build && \
		cd build && \
		cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" && \
		make -j$$(nproc)
	@echo "✓ Build complete: blackbox-server/build/blackbox-server"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf blackbox-server/build
	@echo "✓ Clean complete"

# Install server (requires sudo)
install: build
	@echo "Installing blackbox-server..."
	@sudo cp blackbox-server/build/blackbox-server /usr/local/bin/blackbox-server
	@sudo chmod +x /usr/local/bin/blackbox-server
	@echo "✓ Installed to /usr/local/bin/blackbox-server"

# Run server (for testing)
run: build
	@echo "Running blackbox-server on port 6767..."
	@blackbox-server/build/blackbox-server 6767

# Help
help:
	@echo "Blackbox Server Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build blackbox-server (default)"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make install  - Install server to /usr/local/bin"
	@echo "  make run      - Build and run server on port 6767"
	@echo "  make help     - Show this help"

