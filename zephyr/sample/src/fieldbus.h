struct fieldbus {
	ecx_contextt	context;
	int		roundtrip_time;
	bool		started;
	struct k_mutex	lock;

	ecx_portt	port;
	ec_slavet	slavelist[EC_MAXSLAVE];
	int		slavecount;
	ec_groupt	grouplist[EC_MAXGROUP];
	uint8		esibuf[EC_MAXEEPBUF];
	uint32		esimap[EC_MAXEEPBITMAP];
	ec_eringt	elist;
	ec_idxstackT	idxstack;
	boolean		ecaterror;
	int64		DCtime;
	ec_SMcommtypet	SMcommtype[EC_MAX_MAPT];
	ec_PDOassignt	PDOassign[EC_MAX_MAPT];
	ec_PDOdesct	PDOdesc[EC_MAX_MAPT];
	ec_eepromSMt	eepSM;
	ec_eepromFMMUt	eepFMMU;
};

void fieldbus_initialize(struct fieldbus *fb);
int fieldbus_roundtrip(struct fieldbus *fb);
bool fieldbus_start(struct fieldbus *fb, uint8_t *map);
void fieldbus_stop(struct fieldbus *fb);
