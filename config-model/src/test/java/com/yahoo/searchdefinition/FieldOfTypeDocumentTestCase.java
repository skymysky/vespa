// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.searchdefinition;

import com.yahoo.document.*;
import com.yahoo.document.config.DocumentmanagerConfig;
import com.yahoo.searchdefinition.derived.Deriver;
import com.yahoo.searchdefinition.parser.ParseException;
import org.junit.Test;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;

/**
 * @author Einar M R Rosenvinge
 */
public class FieldOfTypeDocumentTestCase extends SearchDefinitionTestCase {

    @Test
    public void testDocument() throws IOException, ParseException {

        List<String> sds = new ArrayList<>();
        sds.add("src/test/examples/music.sd");
        sds.add("src/test/examples/fieldoftypedocument.sd");
        DocumentmanagerConfig.Builder value = Deriver.getDocumentManagerConfig(sds);
        assertConfigFile("src/test/examples/fieldoftypedocument.cfg",
                         new DocumentmanagerConfig(value).toString() + "\n");

        DocumentTypeManager manager = new DocumentTypeManager();
        DocumentTypeManagerConfigurer.configure(manager, "raw:" + new DocumentmanagerConfig(value).toString());


        DocumentType musicType = manager.getDocumentType("music");
        assertEquals(5, musicType.getFieldCount());

        Field intField = musicType.getField("intfield");
        assertEquals(DataType.INT, intField.getDataType());
        Field stringField = musicType.getField("stringfield");
        assertEquals(DataType.STRING, stringField.getDataType());
        Field longField = musicType.getField("longfield");
        assertEquals(DataType.LONG, longField.getDataType());
        Field summaryfeatures = musicType.getField("summaryfeatures");
        assertEquals(DataType.STRING, summaryfeatures.getDataType());
        Field rankfeatures = musicType.getField("rankfeatures");
        assertEquals(DataType.STRING, rankfeatures.getDataType());


        DocumentType bookType = manager.getDocumentType("book");
        assertEquals(3, bookType.getFieldCount());

        Field musicField = bookType.getField("soundtrack");
        assertSame(musicType, musicField.getDataType());
        summaryfeatures = musicType.getField("summaryfeatures");
        assertEquals(DataType.STRING, summaryfeatures.getDataType());
        rankfeatures = musicType.getField("rankfeatures");
        assertEquals(DataType.STRING, rankfeatures.getDataType());
    }

}

