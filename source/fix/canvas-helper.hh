#if 0
static void debugBuffer(OpenGLFunctions *funcs, GLuint buf) {
	GLint size, immu, flags, usage;
	gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, buf);
	gl(GetBufferParameteriv, funcs)(GL_ARRAY_BUFFER, GL_BUFFER_IMMUTABLE_STORAGE, &immu);
	gl(GetBufferParameteriv, funcs)(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
	gl(GetBufferParameteriv, funcs)(GL_ARRAY_BUFFER, GL_BUFFER_STORAGE_FLAGS, &flags);
	gl(GetBufferParameteriv, funcs)(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &usage);
	fprintf(stderr, "Buffer %d: size %d immu %d flags %d usage %d\n", buf, size, immu, flags, usage);
}

	void loadColormap(const QString& fn) {
		QFile f{fn};
		if(!f.open(QIODevice::ReadOnly))
			throwError("Failed to open");
		QTextStream fs{&f};
		std::vector<QColor> cols;
		while(true) {
			auto line=fs.readLine();
			if(line.isNull()) {
				if(fs.atEnd())
					break;
				throwError("Failed to read line");
			}
			if(line.isEmpty())
				continue;
			//if(line[0]=='#')
			//continue;
			QColor col{line};
			if(!col.isValid())
				throwError("Error color: ", line);
			cols.push_back(col);
		}
		std::swap(colormap, cols);
	}
#endif
