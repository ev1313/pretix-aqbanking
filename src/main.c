#include <time.h>
#include <stdbool.h>

#include <aqbanking/banking.h>
#include <gwenhywfar/gwenhywfar.h>
#include <gwenhywfar/cgui.h>
#include <aqbanking/jobgettransactions.h>
#include <curl/curl.h>

// djb2
unsigned long hash(unsigned char *str)
{
  if(!str) {
    return NULL;
  }

  unsigned long hash = 5381;
  int c;

  while (c = *str++)
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

// adapted from cjson
//
// this escapes the input_str to a valid json string.
//
// into output and output_length the string and it's size respectively will be written.
//
// input_str may be NULL, an empty json string is then returned.
static bool escape_json(const char* input_str, char** output, size_t* output_length)
{
  if (output == NULL || output_length == NULL)
    {
      return false;
    }

  const char empty[] = "\"\"";

  if(input_str == NULL) {
    *output = malloc(sizeof(empty));
    strcpy(*output, empty);
    *output_length = sizeof(empty);
    return true;
  }

  const char *input_pointer = NULL;
  char *output_pointer = NULL;
  char* output_str = NULL;
  size_t output_len = 0;
  size_t escape_characters = 0;

  for (input_pointer = input_str; *input_pointer; input_pointer++)
    {
      switch (*input_pointer)
        {
        case '\"':
        case '\\':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
          /* one character escape sequence */
          escape_characters++;
          break;
        default:
          if (*input_pointer < 32)
            {
              /* UTF-16 escape sequence uXXXX */
              escape_characters += 5;
            }
          break;
        }
    }

  output_len = (size_t)(input_pointer - input_str) + escape_characters;
  output_str = malloc(output_len + sizeof(empty));

  if (escape_characters == 0)
    {
      snprintf(output_str, output_len + sizeof(empty), "\"%s\"", input_str);
      *output = output_str;
      *output_length = output_len;
      return true;
    }

  output_str[0] = '\"';
  output_pointer = output_str + 1;

  /* copy the string */
  for (input_pointer = input_str; *input_pointer != '\0'; (void)input_pointer++, output_pointer++)
    {
      if ((*input_pointer > 31) && (*input_pointer != '\"') && (*input_pointer != '\\'))
        {
          /* normal character, copy */
          *output_pointer = *input_pointer;
        }
      else
        {
          /* character needs to be escaped */
          *output_pointer++ = '\\';
          switch (*input_pointer)
            {
            case '\\':
              *output_pointer = '\\';
              break;
            case '\"':
              *output_pointer = '\"';
              break;
            case '\b':
              *output_pointer = 'b';
              break;
            case '\f':
              *output_pointer = 'f';
              break;
            case '\n':
              *output_pointer = 'n';
              break;
            case '\r':
              *output_pointer = 'r';
              break;
            case '\t':
              *output_pointer = 't';
              break;
            default:
              /* escape and print as unicode codepoint */
              sprintf((char*)output_pointer, "u%04x", *input_pointer);
              output_pointer += 4;
              break;
            }
        }
    }
  output_str[output_len + 1] = '\"';
  output_str[output_len + 2] = '\0';

  *output = output_str;
  *output_length = output_len + sizeof(empty);
  return true;
}

static void* join_ab_strings_cb(const char *str, void* user_data)
{
  char **acc = user_data;
  char *tmp;

  if (!str || !*str)
    return NULL;

  tmp = strdup(str);
  //strstrip(tmp);
  //gnc_utf8_strip_invalid(tmp);

  if(tmp && *acc)
    {
      char *join = malloc(strlen(tmp) + strlen(*acc) + 1);
      if(!join) {
        free(tmp);
        return NULL;
      }
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

void list_transactions(AB_BANKING* ab, AB_ACCOUNT* a, int send, const char* pretix_event, const char* pretix_url, const char* pretix_token) {
  if(!a) {
    fprintf(stderr, "Bank account not found!\n");
    return;
  }

  CURL *curl;
  CURLcode res;

  if(send) {
    if(!pretix_url) {
      fprintf(stderr, "no url");
      return;
    }
    if(!pretix_token) {
      fprintf(stderr, "no token");
      return;
    }

    /* In windows, this will init the winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);
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
    unsigned long i = 0;

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
        const char* trans_currency = NULL;

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
        char trans_date[20];
        struct tm* tm_info;
        tm_info = localtime(&trans_time);
        strftime(trans_date, sizeof(trans_date), "%Y-%m-%d", tm_info);

        trans_value = AB_Value_GetValueAsDouble(v);
        trans_currency = AB_Value_GetCurrency(v);

        unsigned long trans_hash = hash(trans_remote_name) + hash(trans_purpose) + hash(trans_date) + hash(trans_currency);

        if(!send) 
          {
            fprintf(stdout, "transaction %lu - name: (%s) purpose: (%s) date: (%s) value: (%.2f %s)\n",
                    trans_hash,
                    trans_remote_name,
                    trans_purpose,
                    trans_date,
                    trans_value,
                    trans_currency);
          } else {
          // obviously this could all be done in a single post request, TODO
          curl = curl_easy_init();
          if(curl) {
            struct curl_slist *headers = NULL;

            char token_str[1024];
            snprintf(token_str, sizeof(token_str), "Authorization: Token %s", pretix_token);

            char json_str[4096];

            char* json_trans_remote_name = NULL;
            size_t json_trans_remote_name_size = 0;
            escape_json(trans_remote_name, &json_trans_remote_name, &json_trans_remote_name_size);

            char* json_trans_purpose = NULL;
            size_t json_trans_purpose_size = 0;
            escape_json(trans_purpose, &json_trans_purpose, &json_trans_purpose_size);

            snprintf(json_str, sizeof(json_str), "{ \"event\": \"%s\", \"transactions\" : [ { \"payer\": %s, \"reference\": %s, \"amount\": \"%.2f\", \"date\": \"%s\" } ] }",
                     pretix_event,
                     json_trans_remote_name,
                     json_trans_purpose,
                     trans_value,
                     trans_date);

	    printf("sending:\n");
	    printf(json_str);
	    printf("\n");

            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, token_str);
            headers = curl_slist_append(headers, "charsets: utf-8");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_URL, pretix_url);
            curl_easy_setopt(curl, CURLOPT_REFERER, pretix_url);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);

            res = curl_easy_perform(curl);
            if(res != CURLE_OK)
              fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

            free(json_trans_remote_name);
            free(json_trans_purpose);

            curl_easy_cleanup(curl);
          }

        }

        if(trans_remote_name) {
          free(trans_remote_name); 
        }
        if(trans_purpose) {
          free(trans_purpose);
        }
      }
      t=AB_ImExporterAccountInfo_GetNextTransaction(ai);
    }
    ai=AB_ImExporterContext_GetNextAccountInfo(ctx);
  } 
  AB_Job_free(j);
  AB_Job_List2_free(jl);
  AB_ImExporterContext_free(ctx);

  if(send) {
    curl_global_cleanup();
  }

}

int main(int argc, char **argv) {
  // help text
  if(argc>0) {
    if(argc == 1 || strcmp(argv[1], "--help") == 0) {
      fprintf(stdout, "--list - lists all available accounts\n");
      fprintf(stdout, "--list_transactions [Account Number (wildcard possible)] - lists transactions from account identified by account number\n");
      fprintf(stdout, "--send_transactions [Account Number (wildcard possible)] [pretix event name] [pretix api url] [pretix api token] - send transactions from account to pretix\n");
      return 0;
    }
  }

  // initialize command line GUI
  GWEN_GUI *gui = GWEN_Gui_CGui_new();
  GWEN_Gui_SetGui(gui);
  //GWEN_Gui_SetFlags(gui, GWEN_GUI_FLAGS_NONINTERACTIVE);

  AB_BANKING *ab=AB_Banking_new("pretix", NULL, 0);
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
      AB_ACCOUNT_LIST2* accs = AB_Banking_GetAccounts(ab);
      list_accounts(accs);
      AB_Account_List2_free(accs);

		}
		if(strcmp(argv[1], "--list_transactions") == 0) {
			AB_ACCOUNT* a=AB_Banking_FindAccount(ab,"*","*","*",argv[2],"*");
			fprintf(stdout, "list of transactions:\n");
			list_transactions(ab, a, 0, "", "", "");
			AB_Account_free(a);
		}
		if(strcmp(argv[1], "--send_transactions") == 0) {
			AB_ACCOUNT* a=AB_Banking_FindAccount(ab,"*","*","*",argv[2],"*");
			fprintf(stdout, "list of transactions:\n");
			list_transactions(ab, a, 1, argv[3], argv[4], argv[5]);
			AB_Account_free(a);
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


