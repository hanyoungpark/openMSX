// $Id$

#include <iostream>
#include "CliCommunicator.hh"
#include "CommandController.hh"
#include "Scheduler.hh"

using std::cin;
using std::cout;
using std::flush;

namespace openmsx {

CliCommunicator& CliCommunicator::instance()
{
	static CliCommunicator oneInstance;
	return oneInstance;
}

CliCommunicator::CliCommunicator()
	: lock(1)
{
	cout << "<openmsx-output>" << endl;
}

CliCommunicator::~CliCommunicator()
{
	cout << "</openmsx-output>" << endl;
}

void CliCommunicator::printInfo(const string& message)
{
	cout << "<info>" << message << "</info>" << endl;
}

void CliCommunicator::printWarning(const string& message)
{
	cout << "<warning>" << message << "</warning>" << endl;
}

void CliCommunicator::printUpdate(const string& message)
{
	cout << "<update>" << message << "</update>" << endl;
}


// XML-input handling

static xmlSAXHandler sax_handler;
static xmlParserCtxt* parser_context;
CliCommunicator::ParseState CliCommunicator::user_data;

void CliCommunicator::cb_start_element(ParseState* user_data,
                     const xmlChar* name, const xmlChar** attrs)
{
	switch (user_data->state) {
		case START:
			if (strcmp((const char*)name, "openmsx-control") == 0) {
				user_data->state = TAG_OPENMSX;
			}
			break;
		case TAG_OPENMSX:
			if (strcmp((const char*)name, "command") == 0) {
				user_data->state = TAG_COMMAND;
			}
			break;
		default:
			break;
	}
	user_data->content.clear();
}

void CliCommunicator::cb_end_element(ParseState* user_data, const xmlChar* name)
{
	switch (user_data->state) {
		case TAG_OPENMSX:
			user_data->state = START;
			break;
		case TAG_COMMAND:
			CliCommunicator::instance().execute(user_data->content);
			user_data->state = TAG_OPENMSX;
			break;
		default:
			break;
	}
}

void CliCommunicator::cb_text(ParseState* user_data, const xmlChar* chars, int len)
{
	if (user_data->state == TAG_COMMAND) {
		user_data->content.append((const char*)chars, len);
	}
}

void CliCommunicator::run()
{
	user_data.state = START;
	memset(&sax_handler, 0, sizeof(sax_handler));
	sax_handler.startElement = (startElementSAXFunc)cb_start_element;
	sax_handler.endElement   = (endElementSAXFunc)  cb_end_element;
	sax_handler.characters   = (charactersSAXFunc)  cb_text;
	
	parser_context = xmlCreatePushParserCtxt(&sax_handler, &user_data, 0, 0, 0);
	
	string line;
	while (getline(cin, line)) {
		xmlParseChunk(parser_context, line.c_str(), line.length(), 0);
	}

	xmlFreeParserCtxt(parser_context);
}

void CliCommunicator::execute(const string& command)
{
	lock.down();
	cmds.push_back(command);
	lock.up();
	Scheduler::instance()->setSyncPoint(Scheduler::ASAP, this);
}

void CliCommunicator::executeUntilEmuTime(const EmuTime& time, int userData)
{
	CommandController* controller = CommandController::instance();
	lock.down();
	while (!cmds.empty()) {
		try {
			string result = controller->executeCommand(cmds.front());
			cout << "<ok>" << result << "</ok>" << endl;
		} catch (CommandException &e) {
			string result = e.getMessage() + '\n';
			cout << "<nok>" << result << "</nok>" << endl;
		}
		cmds.pop_front();
	}
	lock.up();
}

const string& CliCommunicator::schedName() const
{
	static const string NAME("CliCommunicator");
	return NAME;
}

} // namespace openmsx
