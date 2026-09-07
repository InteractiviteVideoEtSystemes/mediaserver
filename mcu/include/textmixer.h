#ifndef _TEXTMIXER_H_
#define _TEXTMIXER_H_
#include <mutex>
#include "worker.h"
#include "text.h"
#include "textmixerworker.h"
#include "pipetextinput.h"
#include "pipetextoutput.h"
#include "pipeaudioinput.h"
#include "textmixerworker.h"
#include <map>
#include <set>
#include <memory>
using namespace std;

class TextMixer : public Worker
{
public:
	TextMixer();
	~TextMixer();

	//Global members
	static int GlobalInit();
	static int GlobalEnd();

	int Init();
	int CreateMixer(int id,std::wstring &name);
	int SetDisplayName(int id,const std::wstring &displayName);
	int InitMixer(int id);
	int EndMixer(int id);
	int DeleteMixer(int id);
	TextInput*  GetInput(int id);
	TextOutput* GetOutput(int id);
	//Co-propriété (Point 1 / C-4) : copies de shared_ptr sur les pipes.
	std::shared_ptr<TextInput>  GetSharedInput(int id);
	std::shared_ptr<TextOutput> GetSharedOutput(int id);
	int CreatePrivate(int id,int to,std::wstring &name);
	int InitPrivate(int id);
	int EndPrivate(int id);
	int DeletePrivate(int id);
	TextOutput* GetPrivateOutput(int id);
	int End();

protected:
	//Mix thread (corps du Worker)
	virtual int Run();

private:
	struct TextSource
	{
		DWORD id;
		//Le nom de creation, et l'etiquette que le controleur a posee par
		//dessus. Retenue ICI aussi : un participant qui rejoint plus tard la
		//recopie dans son worker (InitMixer).
		std::wstring	name;
		std::wstring	displayName;
		std::shared_ptr<PipeTextInput>	input;
		std::shared_ptr<PipeTextOutput>	output;
		//Le worker est co-detenu avec la liste `workers` : il porte un pointeur
		//BRUT vers `input` (AddReader), donc il doit mourir avant lui. C'est le
		//cas ici — dernier membre declare, donc premier detruit.
		std::shared_ptr<TextMixerWorker> worker;
	};

	struct TextPrivate
	{
		DWORD id;
		DWORD to;
		std::wstring	name;
		std::shared_ptr<PipeTextOutput>	output;
	};

	typedef std::map<DWORD,std::shared_ptr<TextSource>> TextSources;
	typedef std::map<DWORD,std::shared_ptr<TextPrivate>> TextPrivates;
	typedef std::set<std::shared_ptr<TextMixerWorker>> TextWorkers;

private:
	//All the mixer participants
	//`workers` est declare APRES `sources` : detruit avant, il rend ses parts
	//aux sources, qui detruisent alors worker puis pipes dans le bon ordre.
	TextSources	sources;
	TextWorkers	workers;
	TextPrivates	privates;
	int		mixingText;
	//Un seul verrou pour les trois collections ET pour les workers qu'elles
	//portent : TextMixerWorker n'a aucune synchronisation propre, et la passe de
	//mixage l'itere pendant que les chemins de controle le modifient.
	std::mutex	mutex;
};

#endif
