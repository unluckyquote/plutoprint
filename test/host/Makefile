CC = cc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Werror

TEST_SOURCES = $(wildcard test_*.c)
TESTS = $(TEST_SOURCES:.c=)

.PHONY: test clean selftest

test: $(TESTS)
	@failed=""; for test in $(TESTS); do \
		if ! ./$$test; then \
			echo "FAILED: $$test" 1>&2; \
			failed="$$failed $$test"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "Tests failed:$$failed" 1>&2; \
		exit 1; \
	fi

selftest:
	@dir=$$(mktemp -d); \
	echo 'int main(void){return 1;}' > $$dir/fail.c; \
	$(CC) $$dir/fail.c -o $$dir/fail; \
	failed=""; \
	if ! $$dir/fail; then \
		failed="fail"; \
	fi; \
	rm -rf $$dir; \
	if [ -n "$$failed" ]; then \
		echo "selftest ok"; \
	else \
		echo "selftest: did not detect failure" 1>&2; \
		exit 1; \
	fi

test_%: test_%.c
	$(CC) $(CFLAGS) -I ../../components/$* $< ../../components/$*/$*.c -o $@

clean:
	rm -f $(TESTS)
