//
// parser.cc
//
// Implementation of parser
//
//
#if RELEASE
static char RCSid[] = "$Id: parser.cc,v 1.11 1999/07/19 02:05:38 ghutchis Exp $";
#endif

#include "parser.h"
#include "HtPack.h"

#define	WORD	1000
#define	DONE	1001


//*****************************************************************************
Parser::Parser()
{
    tokens = 0;
    result = 0;
    current = 0;
    dbf = 0;
    valid = 1;
}


//*****************************************************************************
// int Parser::checkSyntax(List *tokenList, Database *dbf)
//   As the name of the function implies, we will only perform a syntax check
//   on the list of tokens.
//
int
Parser::checkSyntax(List *tokenList)
{
    tokens = tokenList;
    valid = 1;
    fullexpr(0);
    return valid;
}

//*****************************************************************************
void
Parser::fullexpr(int output)
{
    tokens->Start_Get();
    lookahead = lexan();
    expr(output);
    if (valid && lookahead != DONE)
    {
	setError("end of expression");
    }
}

//*****************************************************************************
int
Parser::lexan()
{
    current = (WeightWord *) tokens->Get_Next();
    if (!current)
	return DONE;
    else if (mystrcasecmp(current->word, "&") == 0)
	return '&';
//	else if (mystrcasecmp(current->word, "and") == 0)
//		return '&';
    else if (mystrcasecmp(current->word, "|") == 0)
	return '|';
    else if (mystrcasecmp(current->word, "!") == 0)
	return '!';
//	else if (mystrcasecmp(current->word, "or") == 0)
//		return '|';
    else if (mystrcasecmp(current->word, "(") == 0)
	return '(';
    else if (mystrcasecmp(current->word, ")") == 0)
	return ')';
    else
	return WORD;
}

//*****************************************************************************
//   Attempt to deal with expressions in the form
//		term | term | term ...
//
void
Parser::expr(int output)
{
    term(output);
    while (1)
    {
	if (match('|'))
	{
	    term(output);
	    if (output)
	    {
		perform_or();
	    }
	}
	else
	    break;
    }
    if (valid && lookahead == WORD)
    {
	setError("'AND' or 'OR'");
    }
}

//*****************************************************************************
void
Parser::term(int output)
{
    int	isand;
    
    factor(output);
    while (1)
    {
	if ((isand = match('&')) || match('!'))
	{
	    factor(output);
	    if (output)
	    {
		perform_and(isand);
	    }
	}
	else
	    break;
    }
}

//*****************************************************************************
void
Parser::factor(int output)
{
    if (match('('))
    {
	expr(output);
	if (match(')'))
	{
	    return;
	}
	else
	{
	    setError("')'");
	}
    }
    else if (lookahead == WORD)
    {
	if (output)
	{
	    perform_push();
	}
	lookahead = lexan();
    }
    else
    {
	setError("a search word");
    }
}

//*****************************************************************************
int
Parser::match(int t)
{
    if (lookahead == t)
    {
	lookahead = lexan();
	return 1;
    }
    else
	return 0;
}

//*****************************************************************************
void
Parser::setError(char *expected)
{
    if (valid)
    {
	valid = 0;
	error = 0;
	error << "Expected " << expected;
	if (lookahead == DONE || !current)
	{
	    error << " at the end";
	}
	else
	{
	    error << " instead of '" << current->word.get();
	    error << '\'';
	    switch (lookahead)
	    {
	    case '&':	error << " or 'AND'";	break;
	    case '|':	error << " or 'OR'";	break;
	    case '!':	error << " or 'NOT'";	break;
	    }
	}
    }
}

//*****************************************************************************
// Perform a lookup of the current word and push the result onto the stack
//
void
Parser::perform_push()
{
    String	temp = current->word.get();
    String	data;
    String	decompressed;
    char	*p;
    ResultList	*list = new ResultList;
    WordRecord	wr;
    DocMatch	*dm;

    stack.push(list);
    if (current->isIgnore)
    {
	//
	// This word needs to be ignored.  Make it so.
	//
	list->isIgnore = 1;
	return;
    }
    temp.lowercase();
    p = temp.get();
    if (temp.length() > MAX_WORD_LENGTH)
	p[MAX_WORD_LENGTH] = '\0';
    if (dbf->Get(p, data) == OK)
    {
	p = data.get();
	char *p_end = p + data.length();
	while (p < p_end)
	{
	  decompressed = htUnpack(WORD_RECORD_COMPRESSED_FORMAT, p);

	  if (decompressed.length() != sizeof (WordRecord))
	  {
            if (debug > 0)
              cerr << "Decode mismatch";

            // We'd better leave now, before we do something worse.
            return;
	  }

	  memcpy((char *) &wr, decompressed.get(), sizeof(WordRecord));

	  //
	  // *******  Compute the score for the document
	  //
	  dm = new DocMatch;
	  dm->score = (wr.flags & FLAG_TEXT) * config.Value("text_factor", 1);
	  dm->score += (wr.flags & FLAG_CAPITAL) * config.Value("caps_factor", 1);
	  dm->score += (wr.flags & FLAG_TITLE) * config.Value("title_factor", 1);
	  dm->score += (wr.flags & FLAG_HEADING) * config.Value("heading1_factor", 1);
	  dm->score += (wr.flags & FLAG_KEYWORDS) * config.Value("keywords_factor", 1);
	  dm->score += (wr.flags & FLAG_DESCRIPTION) * config.Value("meta_description_factor", 1);
	  dm->score += (wr.flags & FLAG_AUTHOR) * config.Value("author_factor", 1);
	  dm->score += (wr.flags & FLAG_LINK_TEXT) * config.Value("description_factor", 1);
	  dm->score *= current->weight;
	  dm->id = wr.id;
	  dm->anchor = wr.anchor;
	  list->add(dm);
	}

	if (p != p_end && debug > 0)
	  cerr << "Decompression out of sync: " << (unsigned int) p
	       << " != " << (unsigned int) p_end << endl;
    }
}

//*****************************************************************************
// The top two entries in the stack need to be ANDed together.
//
void
Parser::perform_and(int isand)
{
    ResultList		*l1 = (ResultList *) stack.pop();
    ResultList		*l2 = (ResultList *) stack.pop();
    ResultList		*result = new ResultList;
    int			i;
    DocMatch		*dm, *dm2, *dm3;
    HtVector		*elements;

    //
    // If either of the arguments is not present, we will use the other as
    // the result.
    //
    if (!l1 && l2)
    {
	stack.push(l2);
	return;
    }
    else if (l1 && !l2)
    {
	stack.push(l1);
	return;
    }
    else if (!l1 && !l2)
    {
	stack.push(result);
	return;
    }
    
    //
    // If either of the arguments is set to be ignored, we will use the
    // other as the result.
    //
    if (l1->isIgnore)
    {
	stack.push(l2);
	delete l1;
	return;
    }
    else if (l2->isIgnore)
    {
	stack.push(isand ? l1 : result);
	delete l2;
	return;
    }
    
    stack.push(result);
    elements = l2->elements();
    for (i = 0; i < elements->Count(); i++)
    {
	dm = (DocMatch *) (*elements)[i];
	dm2 = l1->find(dm->id);
	if (dm2 ? isand : (isand == 0))
	{
	    //
	    // Duplicate document.  We just need to add the scored together.
	    //
	    dm3 = new DocMatch;
	    dm3->score = dm->score + (dm2 ? dm2->score : 0);
	    dm3->id = dm->id;
	    dm3->anchor = dm->anchor;
	    if (dm2 && dm2->anchor < dm3->anchor)
		dm3->anchor = dm2->anchor;
	    result->add(dm3);
	}
    }
    elements->Release();
    delete elements;
    delete l1;
    delete l2;
}

//*****************************************************************************
// The top two entries in the stack need to be ORed together.
//
void
Parser::perform_or()
{
    ResultList		*l1 = (ResultList *) stack.pop();
    ResultList		*result = (ResultList *) stack.peek();
    int			i;
    DocMatch		*dm, *dm2;
    HtVector		*elements;

    //
    // If either of the arguments is not present, we will use the other as
    // the results.
    //
    if (!l1 && result)
    {
	return;
    }
    else if (l1 && !result)
    {
	stack.push(l1);
	return;
    }
    else if (!l1 & !result)
    {
	stack.push(new ResultList);
	return;
    }
    
    //
    // If either of the arguments is set to be ignored, we will use the
    // other as the result.
    //
    if (l1->isIgnore)
    {
	delete l1;
	return;
    }
    else if (result->isIgnore)
    {
	result = (ResultList *) stack.pop();
	stack.push(l1);
	delete result;
	return;
    }
    
    elements = l1->elements();
    for (i = 0; i < elements->Count(); i++)
    {
	dm = (DocMatch *) (*elements)[i];
	dm2 = result->find(dm->id);
	if (dm2)
	{
	    //
	    // Duplicate document.  We just need to add the scores together
	    //
	    dm2->score += dm->score;
	    if (dm->anchor < dm2->anchor)
		dm2->anchor = dm->anchor;
	}
	else
	{
	    dm2 = new DocMatch;
	    dm2->score = dm->score;
	    dm2->id = dm->id;
	    dm2->anchor = dm->anchor;
	    result->add(dm2);
	}
    }
    elements->Release();
    delete elements;
    delete l1;
}

//*****************************************************************************
// void Parser::parse(List *tokenList, ResultList &resultMatches)
//
void
Parser::parse(List *tokenList, ResultList &resultMatches)
{
    tokens = tokenList;
    fullexpr(1);

    ResultList	*result = (ResultList *) stack.pop();
    if (!result)  // Ouch!
      {
	valid = 0;
	error = 0;
	error << "Expected to have something to parse!";
	return;
      }
    HtVector		*elements = result->elements();
    DocMatch	*dm;

    for (int i = 0; i < elements->Count(); i++)
    {
	dm = (DocMatch *) (*elements)[i];
	resultMatches.add(dm);
    }
    elements->Release();
    result->Release();
    delete elements;
    delete result;
}
