#include <time.h>

#include <aqbanking/banking.h>
#include <gwenhywfar/gwenhywfar.h>
#include <gwenhywfar/cgui.h>
#include <aqbanking/jobgettransactions.h>
#include <curl/curl.h>

static void* join_ab_strings_cb(const char *str, void* user_data)
{
	char **acc = user_data;
	char *tmp;

	if (!str || !*str)
		return NULL;

	tmp = strdup(str);
	//strstrip(tmp);
	//gnc_utf8_strip_invalid(tmp);

	if (*acc)
	{
		char *join = malloc(strlen(tmp) + strlen(*acc) + 1);
		strcpy(join, *acc);
		strcat(join, tmp);
		free(*acc);
		free(tmp);
		*acc = join;
	}
	else
	{
		*acc = tmp;
	}
	return NULL;
}

void list_accounts(AB_ACCOUNT_LIST2* accs) {
	if (accs) {
		AB_ACCOUNT_LIST2_ITERATOR *it;

		it=AB_Account_List2_First(accs);
		if (it) {
			unsigned int i = 0;
			AB_ACCOUNT *a;

			a=AB_Account_List2Iterator_Data(it);
			while(a) {
				AB_PROVIDER *pro;

				pro=AB_Account_GetProvider(a);
				fprintf(stdout,
						"Account %d: %s (%s) %s (%s) [%s]\n",
						++i,
						AB_Account_GetBankCode(a),
						AB_Account_GetBankName(a),
						AB_Account_GetAccountNumber(a),
						AB_Account_GetAccountName(a),
						AB_Provider_GetName(pro));
				a=AB_Account_List2Iterator_Next(it);
			}
			AB_Account_List2Iterator_free(it);
		}
		AB_Account_List2_free(accs);
	}
}

void list_transactions(AB_BANKING* ab, AB_ACCOUNT* a, int send, const char* pretix_url, const char* pretix_token) {
	if(!a) {
		fprintf(stderr, "Bank account not found!\n");
		return;
	}

	if(send) {
		if(!pretix_url) {
			fprintf(stderr, "no url");
			return;
		}
		if(!pretix_token) {
			fprintf(stderr, "no token");
			return;
		}
	}

	AB_JOB_LIST2 *jl;
	AB_JOB *j;
	AB_IMEXPORTER_CONTEXT *ctx;

	j=AB_JobGetTransactions_new(a);

	int rv = AB_Job_CheckAvailability(j);
	if (rv) {
		fprintf(stderr, "Job is not available (%d)\n", rv);
		return;
	}

	jl=AB_Job_List2_new();

	AB_Job_List2_PushBack(jl, j);

	ctx=AB_ImExporterContext_new();

	rv=AB_Banking_ExecuteJobs(ab, jl, ctx);
	if (rv) {
		fprintf(stderr, "Error on executeQueue (%d)\n", rv);
		return;
	}

	AB_IMEXPORTER_ACCOUNTINFO *ai;

	ai=AB_ImExporterContext_GetFirstAccountInfo(ctx);
	while(ai) {
		unsigned int i = 0;

		const AB_TRANSACTION *t;

		t=AB_ImExporterAccountInfo_GetFirstTransaction(ai);
		while(t) {
			const AB_VALUE *v;

			v=AB_Transaction_GetValue(t);
			if (v) {
				i++;
				//purpose=(const char*)AB_Transaction_GetTransactionText(t);

				char *trans_remote_name = NULL;
				char *trans_purpose = NULL;
				time_t trans_time = 0;
				double trans_value = 0.0;
				char* trans_currency = NULL;

				const GWEN_STRINGLIST* ab_remote_name = AB_Transaction_GetRemoteName(t);
				if (ab_remote_name)
					GWEN_StringList_ForEach(ab_remote_name, join_ab_strings_cb, &trans_remote_name);

				const GWEN_STRINGLIST* ab_purpose = AB_Transaction_GetPurpose(t);
				if (ab_purpose)
					GWEN_StringList_ForEach(ab_purpose, join_ab_strings_cb, &trans_purpose);

				const GWEN_TIME* valuta_date = AB_Transaction_GetValutaDate(t);
				if (!valuta_date)
				{
					const GWEN_TIME *normal_date = AB_Transaction_GetDate(t);
					if (normal_date)
						valuta_date = normal_date;
				}
				if(valuta_date) {
					trans_time = GWEN_Time_toTime_t(valuta_date);
				} else {
					fprintf(stderr, "no date could be acquired for this transaction!\n");
				}

				// time to human readable string
				char buffer[26];
				struct tm* tm_info;
				tm_info = localtime(&trans_time);
				strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

				trans_value = AB_Value_GetValueAsDouble(v);
				trans_currency = AB_Value_GetCurrency(v);
				
				fprintf(stdout, "transaction %lu - name: (%s) purpose: (%s) date: (%s) value: (%.2f %s)\n",
						i,
						trans_remote_name,
						trans_purpose,
						buffer,
						trans_value,
						trans_currency);
			}
			t=AB_ImExporterAccountInfo_GetNextTransaction(ai);
		}
		ai=AB_ImExporterContext_GetNextAccountInfo(ctx);
	} 
	AB_Job_free(j);
}

int main(int argc, char **argv) {
	// help text
	if(argc>1) {
		if(strcmp(argv[1], "--help") == 0) {
			fprintf(stdout, "--list - lists all available accounts\n");
			fprintf(stdout, "--list_transactions [Account Number (wildcard possible)] - lists transactions from account identified by account number\n");
			fprintf(stdout, "--send_transactions [Account Number (wildcard possible)] [pretix api url] [pretix api token] - send transactions from account to pretix\n");
			return 0;
		}
	}

	// initialize command line GUI
	GWEN_GUI *gui = GWEN_Gui_CGui_new();
	GWEN_Gui_SetGui(gui);

	AB_BANKING *ab=AB_Banking_new("pretix", 0, 0);
	// potential errorcode
	int rv=AB_Banking_Init(ab);
	if (rv) {
		fprintf(stderr, "Error on init (%d)\n", rv);
		return 2;
	}

	rv=AB_Banking_OnlineInit(ab);
	if (rv) {
		fprintf(stderr, "Error on onlineinit (%d)\n", rv);
		return 2;
	}

	if(argc>1) {
		if(strcmp(argv[1], "--list") == 0) {
			fprintf(stdout, "list of available banking accounts: \n");
			list_accounts(AB_Banking_GetAccounts(ab));
		}
		if(strcmp(argv[1], "--list_transactions") == 0) {
			AB_ACCOUNT* a=AB_Banking_FindAccount(ab,"*","*","*",argv[2],"*");
			fprintf(stdout, "list of transactions:\n");
			list_transactions(ab, a, 0, "", "");
		}
		if(strcmp(argv[1], "--send_transactions") == 0) {
			AB_ACCOUNT* a=AB_Banking_FindAccount(ab,"*","*","*",argv[2],"*");
			fprintf(stdout, "list of transactions:\n");
			list_transactions(ab, a, 1, argv[3], argv[4]);
		}
	}

	// cleanup
	rv=AB_Banking_OnlineFini(ab);
	if (rv) {
		fprintf(stderr, "ERROR: Error on online deinit (%d)\n", rv);
		return 3;
	}

	rv=AB_Banking_Fini(ab);
	if (rv) {
		fprintf(stderr, "ERROR: Error on deinit (%d)\n", rv);
		return 3;
	}
	AB_Banking_free(ab);
	return 0;
}


