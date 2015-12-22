#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <string.h>
#include <time.h>
#if defined(__APPLE__)
	#include <sys/time.h>
#endif

struct logger {
	FILE * handle;
	int close;
};

struct logger *
logger_create(void) {
	struct logger * inst = skynet_malloc(sizeof(*inst));
	inst->handle = NULL;
	inst->close = 0;
	return inst;
}

void
logger_release(struct logger * inst) {
	if (inst->close) {
		fclose(inst->handle);
	}
	skynet_free(inst);
}

static int
_logger(struct skynet_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct logger * inst = ud;
	fprintf(inst->handle, "[:%08x] ",source);
	fwrite(msg, sz , 1, inst->handle);
	fprintf(inst->handle, "\n");
	fflush(inst->handle);

	return 0;
}

int
logger_init(struct logger * inst, struct skynet_context *ctx, const char * parm) {
	if (parm) {
		if (!strchr(parm, '.')) {
			char tmp1[64], tmp2[128];
			time_t t = time(NULL);
			strftime(tmp1, 64, "%Y-%m%d-%H%M", localtime(&t));
			sprintf(tmp2, "%s-%s", parm, tmp1);
			inst->handle = fopen(tmp2, "w");
		} else {
			inst->handle = fopen(parm, "w");
		}
		if (inst->handle == NULL) {
			return 1;
		}
		inst->close = 1;
	} else {
		inst->handle = stdout;
	}
	if (inst->handle) {
		skynet_callback(ctx, inst, _logger);
		skynet_command(ctx, "REG", ".logger");
		return 0;
	}
	return 1;
}
